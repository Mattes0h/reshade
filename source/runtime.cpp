#include "log.hpp"
#include "version.h"
#include "runtime.hpp"
#include "runtime_objects.hpp"
#include "hook_manager.hpp"
#include "parser.hpp"
#include "preprocessor.hpp"
#include "input.hpp"
#include "file_watcher.hpp"
#include "string_utils.hpp"
#include "ini_file.hpp"
#include "filesystem.hpp"

#include <stb_image.h>
#include <stb_image_dds.h>
#include <stb_image_write.h>
#include <stb_image_resize.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

extern bool g_blockSetCursorPos;

namespace reshade
{
	namespace
	{
		std::streamsize stream_size(std::istream &s)
		{
			s.seekg(0, std::ios::end);
			const auto size = s.tellg();
			s.seekg(0, std::ios::beg);
			return size;
		}

		filesystem::path s_executable_path, s_injector_path, s_appdata_path;
		std::string s_executable_name, s_imgui_ini_path;

		const char keyboard_keys[256][16] = {
			"", "", "", "Cancel", "", "", "", "", "Backspace", "Tab", "", "", "Clear", "Enter", "", "",
			"Shift", "Control", "Alt", "Pause", "Caps Lock", "", "", "", "", "", "", "Escape", "", "", "", "",
			"Space", "Page Up", "Page Down", "End", "Home", "Left Arrow", "Up Arrow", "Right Arrow", "Down Arrow", "Select", "", "", "Print Screen", "Insert", "Delete", "Help",
			"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "", "", "", "", "", "",
			"", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
			"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "Left Windows", "Right Windows", "", "", "Sleep",
			"Numpad 0", "Numpad 1", "Numpad 2", "Numpad 3", "Numpad 4", "Numpad 5", "Numpad 6", "Numpad 7", "Numpad 8", "Numpad 9", "Numpad *", "Numpad +", "", "Numpad -", "Numpad Decimal", "Numpad /",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "F13", "F14", "F15", "F16",
			"F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24", "", "", "", "", "", "", "", "",
			"Num Lock", "Scroll Lock",
		};
	}

	template <>
	annotation::annotation(const ImVec2 &value) : _values(2)
	{
		_values[0] = std::to_string(value.x);
		_values[1] = std::to_string(value.y);
	}
	template <>
	annotation::annotation(const ImVec4 &value) : _values(4)
	{
		_values[0] = std::to_string(value.x);
		_values[1] = std::to_string(value.y);
		_values[2] = std::to_string(value.z);
		_values[3] = std::to_string(value.w);
	}
	template <>
	inline const ImVec2 annotation::as(size_t i) const
	{
		return ImVec2(as<float>(i), as<float>(i + 1));
	}
	template <>
	inline const ImVec4 annotation::as(size_t i) const
	{
		return ImVec4(as<float>(i), as<float>(i + 1), as<float>(i + 2), as<float>(i + 3));
	}

	void runtime::startup(const filesystem::path &executable_path, const filesystem::path &injector_path)
	{
		s_injector_path = injector_path;
		s_executable_path = executable_path;
		s_executable_name = s_executable_path.filename_without_extension();

		filesystem::path log_path(injector_path), tracelog_path(injector_path);
		log_path.replace_extension("log");
		tracelog_path.replace_extension("tracelog");

		if (filesystem::exists(tracelog_path))
		{
			log::debug = true;

			log::open(tracelog_path);
		}
		else
		{
			log::open(log_path);
		}

#ifdef WIN64
#define VERSION_PLATFORM "64-bit"
#else
#define VERSION_PLATFORM "32-bit"
#endif
		LOG(INFO) << "Initializing crosire's ReShade version '" VERSION_STRING_FILE "' (" << VERSION_PLATFORM << ") built on '" VERSION_DATE " " VERSION_TIME "' loaded from " << injector_path << " to " << executable_path << " ...";

		const auto system_path = filesystem::get_special_folder_path(filesystem::special_folder::system);
		s_appdata_path = filesystem::get_special_folder_path(filesystem::special_folder::app_data) / "ReShade";

		if (!filesystem::exists(s_appdata_path))
		{
			filesystem::create_directory(s_appdata_path);
		}

		s_imgui_ini_path = s_appdata_path / "Windows.ini";

		hooks::register_module(system_path / "d3d8.dll");
		hooks::register_module(system_path / "d3d9.dll");
		hooks::register_module(system_path / "d3d10.dll");
		hooks::register_module(system_path / "d3d10_1.dll");
		hooks::register_module(system_path / "d3d11.dll");
		hooks::register_module(system_path / "d3d12.dll");
		hooks::register_module(system_path / "dxgi.dll");
		hooks::register_module(system_path / "opengl32.dll");
		hooks::register_module(system_path / "user32.dll");
		hooks::register_module(system_path / "ws2_32.dll");

		auto &imgui_io = ImGui::GetIO();
		imgui_io.IniFilename = s_imgui_ini_path.c_str();
		imgui_io.KeyMap[ImGuiKey_Tab] = 0x09; // VK_TAB
		imgui_io.KeyMap[ImGuiKey_LeftArrow] = 0x25; // VK_LEFT
		imgui_io.KeyMap[ImGuiKey_RightArrow] = 0x27; // VK_RIGHT
		imgui_io.KeyMap[ImGuiKey_UpArrow] = 0x26; // VK_UP
		imgui_io.KeyMap[ImGuiKey_DownArrow] = 0x28; // VK_DOWN
		imgui_io.KeyMap[ImGuiKey_PageUp] = 0x21; // VK_PRIOR
		imgui_io.KeyMap[ImGuiKey_PageDown] = 0x22; // VK_NEXT
		imgui_io.KeyMap[ImGuiKey_Home] = 0x24; // VK_HOME
		imgui_io.KeyMap[ImGuiKey_End] = 0x23; // VK_END
		imgui_io.KeyMap[ImGuiKey_Delete] = 0x2E; // VK_DELETE
		imgui_io.KeyMap[ImGuiKey_Backspace] = 0x08; // VK_BACK
		imgui_io.KeyMap[ImGuiKey_Enter] = 0x0D; // VK_RETURN
		imgui_io.KeyMap[ImGuiKey_Escape] = 0x1B; // VK_ESCAPE
		imgui_io.KeyMap[ImGuiKey_A] = 'A';
		imgui_io.KeyMap[ImGuiKey_C] = 'C';
		imgui_io.KeyMap[ImGuiKey_V] = 'V';
		imgui_io.KeyMap[ImGuiKey_X] = 'X';
		imgui_io.KeyMap[ImGuiKey_Y] = 'Y';
		imgui_io.KeyMap[ImGuiKey_Z] = 'Z';

		LOG(INFO) << "Initialized.";
	}
	void runtime::shutdown()
	{
		LOG(INFO) << "Exiting ...";

		ImGui::Shutdown();

		input::uninstall();
		hooks::uninstall();

		LOG(INFO) << "Exited.";
	}

	runtime::runtime(uint32_t renderer) :
		_renderer_id(renderer),
		_start_time(std::chrono::high_resolution_clock::now()),
		_shader_edit_buffer(32768)
	{
		load_configuration();
	}
	runtime::~runtime()
	{
		assert(!_is_initialized && _techniques.empty());
	}

	bool runtime::on_init()
	{
		LOG(INFO) << "Recreated runtime environment on runtime " << this << ".";

		_is_initialized = true;

		reload();

		return true;
	}
	void runtime::on_reset()
	{
		on_reset_effect();

		if (!_is_initialized)
		{
			return;
		}

		_imgui_font_atlas.reset();

		LOG(INFO) << "Destroyed runtime environment on runtime " << this << ".";

		_width = _height = 0;
		_is_initialized = false;
	}
	void runtime::on_reset_effect()
	{
		_textures.clear();
		_uniforms.clear();
		_techniques.clear();
		_uniform_data_storage.clear();

		_errors.clear();
		_included_files.clear();
	}
	void runtime::on_present()
	{
		const auto time = std::time(nullptr);
		const auto ticks = std::chrono::high_resolution_clock::now();

		g_network_traffic = 0;
		_framecount++;
		_drawcalls = _vertices = 0;
		_last_frame_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(ticks - _last_present);
		_last_present = ticks;

		tm tm;
		localtime_s(&tm, &time);
		_date[0] = tm.tm_year + 1900;
		_date[1] = tm.tm_mon + 1;
		_date[2] = tm.tm_mday;
		_date[3] = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;

		if ((_menu_index != 1 || !_show_menu) && _input->is_key_pressed(_screenshot_key.keycode, _screenshot_key.ctrl, _screenshot_key.shift, false))
		{
			screenshot();
		}

		draw_overlay();

		_input->next_frame();
	}
	void runtime::on_draw_call(unsigned int vertices)
	{
		_vertices += vertices;
		_drawcalls += 1;
	}
	void runtime::on_apply_effect()
	{
		for (auto &variable : _uniforms)
		{
			const auto it = variable.annotations.find("source");

			if (it == variable.annotations.end())
			{
				continue;
			}

			const auto source = it->second.as<std::string>();

			if (source == "frametime")
			{
				const float value = _last_frame_duration.count() * 1e-6f;
				set_uniform_value(variable, &value, 1);
			}
			else if (source == "framecount" || source == "framecounter")
			{
				switch (variable.basetype)
				{
					case uniform_datatype::bool_:
					{
						const bool even = (_framecount % 2) == 0;
						set_uniform_value(variable, &even, 1);
						break;
					}
					case uniform_datatype::int_:
					case uniform_datatype::uint_:
					{
						const unsigned int framecount = static_cast<unsigned int>(_framecount % UINT_MAX);
						set_uniform_value(variable, &framecount, 1);
						break;
					}
					case uniform_datatype::float_:
					{
						const float framecount = static_cast<float>(_framecount % 16777216);
						set_uniform_value(variable, &framecount, 1);
						break;
					}
				}
			}
			else if (source == "pingpong")
			{
				float value[2] = { 0, 0 };
				get_uniform_value(variable, value, 2);

				const float min = variable.annotations["min"].as<float>(), max = variable.annotations["max"].as<float>();
				const float step_min = variable.annotations["step"].as<float>(0), step_max = variable.annotations["step"].as<float>(1);
				float increment = step_max == 0 ? step_min : (step_min + std::fmodf(static_cast<float>(std::rand()), step_max - step_min + 1));
				const float smoothing = variable.annotations["smoothing"].as<float>();

				if (value[1] >= 0)
				{
					increment = std::max(increment - std::max(0.0f, smoothing - (max - value[0])), 0.05f);
					increment *= _last_frame_duration.count() * 1e-9f;

					if ((value[0] += increment) >= max)
					{
						value[0] = max;
						value[1] = -1;
					}
				}
				else
				{
					increment = std::max(increment - std::max(0.0f, smoothing - (value[0] - min)), 0.05f);
					increment *= _last_frame_duration.count() * 1e-9f;

					if ((value[0] -= increment) <= min)
					{
						value[0] = min;
						value[1] = +1;
					}
				}

				set_uniform_value(variable, value, 2);
			}
			else if (source == "date")
			{
				set_uniform_value(variable, _date, 4);
			}
			else if (source == "timer")
			{
				const unsigned long long timer = std::chrono::duration_cast<std::chrono::nanoseconds>(_last_present - _start_time).count();

				switch (variable.basetype)
				{
					case uniform_datatype::bool_:
					{
						const bool even = (timer % 2) == 0;
						set_uniform_value(variable, &even, 1);
						break;
					}
					case uniform_datatype::int_:
					case uniform_datatype::uint_:
					{
						const unsigned int timer_int = static_cast<unsigned int>(timer % UINT_MAX);
						set_uniform_value(variable, &timer_int, 1);
						break;
					}
					case uniform_datatype::float_:
					{
						const float timer_float = std::fmod(static_cast<float>(timer * 1e-6f), 16777216.0f);
						set_uniform_value(variable, &timer_float, 1);
						break;
					}
				}
			}
			else if (source == "key")
			{
				const int key = variable.annotations["keycode"].as<int>();

				if (key > 0 && key < 256)
				{
					if (variable.annotations["toggle"].as<bool>())
					{
						bool current = false;
						get_uniform_value(variable, &current, 1);

						if (_input->is_key_pressed(key))
						{
							current = !current;

							set_uniform_value(variable, &current, 1);
						}
					}
					else
					{
						const bool state = _input->is_key_down(key);

						set_uniform_value(variable, &state, 1);
					}
				}
			}
			else if (source == "mousepoint")
			{
				const float values[2] = { static_cast<float>(_input->mouse_position_x()), static_cast<float>(_input->mouse_position_y()) };

				set_uniform_value(variable, values, 2);
			}
			else if (source == "mousebutton")
			{
				const int index = variable.annotations["keycode"].as<int>();

				if (index > 0 && index < 3)
				{
					if (variable.annotations["toggle"].as<bool>())
					{
						bool current = false;
						get_uniform_value(variable, &current, 1);

						if (_input->is_mouse_button_pressed(index))
						{
							current = !current;

							set_uniform_value(variable, &current, 1);
						}
					}
					else
					{
						const bool state = _input->is_mouse_button_down(index);

						set_uniform_value(variable, &state, 1);
					}
				}
			}
			else if (source == "random")
			{
				const int min = variable.annotations["min"].as<int>(), max = variable.annotations["max"].as<int>();
				const int value = min + (std::rand() % (max - min + 1));

				set_uniform_value(variable, &value, 1);
			}
		}

		for (auto &technique : _techniques)
		{
			if (technique.toggle_time != 0 && technique.toggle_time == _date[3])
			{
				technique.enabled = !technique.enabled;
				technique.timeleft = technique.timeout;
				technique.toggle_time = 0;
			}
			else if (technique.timeleft > 0)
			{
				technique.timeleft -= static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(_last_frame_duration).count());

				if (technique.timeleft <= 0)
				{
					technique.enabled = !technique.enabled;
					technique.timeleft = 0;
				}
			}
			else if (_input->is_key_pressed(technique.toggle_key, technique.toggle_key_ctrl, technique.toggle_key_shift, technique.toggle_key_alt))
			{
				technique.enabled = !technique.enabled;
				technique.timeleft = technique.timeout;
			}

			if (!technique.enabled)
			{
				technique.average_duration.clear();

				continue;
			}

			const auto time_technique_started = std::chrono::high_resolution_clock::now();

			on_apply_effect_technique(technique);

			technique.average_duration.append(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - time_technique_started).count());
		}
	}
	void runtime::on_apply_effect_technique(const technique &technique)
	{
		for (auto &variable : _uniforms)
		{
			const auto it = variable.annotations.find("source");

			if (it == variable.annotations.end())
			{
				continue;
			}

			const auto source = it->second.as<std::string>();

			if (source == "timeleft")
			{
				set_uniform_value(variable, &technique.timeleft, 1);
			}
		}
	}

	void runtime::reload()
	{
		on_reset_effect();

		for (const auto &search_path : _effect_search_paths)
		{
			const auto files = filesystem::list_files(search_path, "*.fx");

			for (const auto &path : files)
			{
				reshadefx::syntax_tree ast;

				if (!load_effect(path, ast))
				{
					continue;
				}

				if (!update_effect(ast, _errors))
				{
					continue;
				}

				for (auto &variable : _uniforms)
				{
					if (!variable.annotations.count("__FILE__"))
					{
						variable.annotations["__FILE__"] = static_cast<const std::string &>(path);
					}
				}
				for (const auto &texture : _textures)
				{
					if (!texture->annotations.count("__FILE__"))
					{
						texture->annotations["__FILE__"] = static_cast<const std::string &>(path);
					}
				}
				for (auto &technique : _techniques)
				{
					if (!technique.annotations.count("__FILE__"))
					{
						technique.annotations["__FILE__"] = static_cast<const std::string &>(path);

						technique.enabled = technique.annotations["enabled"].as<bool>();
						technique.timeleft = technique.timeout = technique.annotations["timeout"].as<int>();
						technique.toggle_key = technique.annotations["toggle"].as<int>();
						technique.toggle_key_ctrl = technique.annotations["togglectrl"].as<bool>();
						technique.toggle_key_shift = technique.annotations["toggleshift"].as<bool>();
						technique.toggle_key_alt = technique.annotations["togglealt"].as<bool>();
						technique.toggle_time = technique.annotations["toggletime"].as<int>();
					}
				}
			}
		}

		load_textures();

		if (_current_preset >= 0)
		{
			load_preset(_preset_files[_current_preset]);
		}
	}
	void runtime::screenshot()
	{
		const int hour = _date[3] / 3600;
		const int minute = (_date[3] - hour * 3600) / 60;
		const int second = _date[3] - hour * 3600 - minute * 60;

		const std::string extensions[] { ".bmp", ".png" };
		const filesystem::path path = _screenshot_path + '\\' + s_executable_name + ' ' +
			std::to_string(_date[0]) + '-' + std::to_string(_date[1]) + '-' + std::to_string(_date[2]) + ' ' +
			std::to_string(hour) + '-' + std::to_string(minute) + '-' + std::to_string(second) +
			extensions[_screenshot_format];
		std::vector<uint8_t> data(_width * _height * 4);

		screenshot(data.data());

		LOG(INFO) << "Saving screenshot to " << path << " ...";

		FILE *file;
		bool success = false;

		if (_wfopen_s(&file, stdext::utf8_to_utf16(path).c_str(), L"rb") == 0)
		{
			stbi_write_func *func = [](void *context, void *data, int size)
			{
				fwrite(data, 1, size, static_cast<FILE *>(context));
			};

			switch (_screenshot_format)
			{
				case 0:
					success = stbi_write_bmp_to_func(func, file, _width, _height, 4, data.data()) != 0;
					break;
				case 1:
					success = stbi_write_png_to_func(func, file, _width, _height, 4, data.data(), 0) != 0;
					break;
			}

			fclose(file);
		}

		if (!success)
		{
			LOG(ERROR) << "Failed to write screenshot to " << path << "!";
		}
	}
	bool runtime::load_effect(const filesystem::path &path, reshadefx::syntax_tree &ast)
	{
		reshadefx::parser pa(ast, _errors);
		reshadefx::preprocessor pp;

		pp.add_include_path(path.parent_path());

		for (const auto &include_path : _effect_search_paths)
		{
			pp.add_include_path(include_path);
		}

		pp.add_macro_definition("__RESHADE__", std::to_string(VERSION_MAJOR * 10000 + VERSION_MINOR * 100 + VERSION_REVISION));
		pp.add_macro_definition("__VENDOR__", std::to_string(_vendor_id));
		pp.add_macro_definition("__DEVICE__", std::to_string(_device_id));
		pp.add_macro_definition("__RENDERER__", std::to_string(_renderer_id));
		pp.add_macro_definition("__APPLICATION__", std::to_string(std::hash<std::string>()(s_executable_name)));
		pp.add_macro_definition("BUFFER_WIDTH", std::to_string(_width));
		pp.add_macro_definition("BUFFER_HEIGHT", std::to_string(_height));
		pp.add_macro_definition("BUFFER_RCP_WIDTH", std::to_string(1.0f / static_cast<float>(_width)));
		pp.add_macro_definition("BUFFER_RCP_HEIGHT", std::to_string(1.0f / static_cast<float>(_height)));

		if (!pp.run(path, _included_files))
		{
			_errors += pp.current_errors();

			return false;
		}

		_effect_files.push_back(path);
		_included_files.push_back(path);

		for (const auto &pragma : pp.current_pragmas())
		{
			reshadefx::lexer lexer(pragma);

			const auto prefix_token = lexer.lex();

			if (prefix_token.literal_as_string == "message")
			{
				const auto message_token = lexer.lex();

				if (message_token == reshadefx::lexer::tokenid::string_literal)
				{
					_message += message_token.literal_as_string;
				}
				continue;
			}
		}

		if (!pa.run(pp.current_output()))
		{
			return false;
		}

		return true;
	}
	void runtime::load_textures()
	{
		for (const auto &texture : _textures)
		{
			const auto it = texture->annotations.find("source");

			if (it == texture->annotations.end())
			{
				continue;
			}

			const filesystem::path path = filesystem::resolve(it->second.as<std::string>(), _texture_search_paths);

			if (!filesystem::exists(path))
			{
				LOG(ERROR) << "> Source " << path << " for texture '" << texture->name << "' could not be found.";

				continue;
			}

			FILE *file;
			unsigned char *filedata = nullptr;
			int width = 0, height = 0, channels = 0;
			bool success = false;

			texture->storage_size = texture->width * texture->height * 4;

			if (_wfopen_s(&file, stdext::utf8_to_utf16(path).c_str(), L"rb") == 0)
			{
				if (stbi_dds_test_file(file))
				{
					filedata = stbi_dds_load_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
				}
				else
				{
					filedata = stbi_load_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
				}

				fclose(file);
			}

			if (filedata != nullptr)
			{
				if (texture->width != static_cast<unsigned int>(width) || texture->height != static_cast<unsigned int>(height))
				{
					LOG(INFO) << "> Resizing image data for texture '" << texture->name << "' from " << width << "x" << height << " to " << texture->width << "x" << texture->height << " ...";

					std::vector<uint8_t> resized(texture->width * texture->height * 4);
					stbir_resize_uint8(filedata, width, height, 0, resized.data(), texture->width, texture->height, 0, 4);
					success = update_texture(*texture, resized.data());
				}
				else
				{
					success = update_texture(*texture, filedata);
				}

				stbi_image_free(filedata);
			}

			if (!success)
			{
				_errors += "Unable to load source for texture '" + texture->name + "'!";

				LOG(ERROR) << "> Source " << path << " for texture '" << texture->name << "' could not be loaded! Make sure it is of a compatible file format.";
			}
		}
	}
	void runtime::load_configuration()
	{
		const utils::ini_file apps_config(s_appdata_path / "Apps.ini");
		const utils::ini_file style_config(s_appdata_path / "Style.ini");

		_developer_mode = apps_config.get(s_executable_path, "DeveloperMode", false).as<bool>();
		_menu_key.keycode = apps_config.get(s_executable_path, "MenuKey", 0x71).as<int>(); // VK_F2
		_menu_key.ctrl = apps_config.get(s_executable_path, "MenuKeyCtrl", false).as<bool>();
		_menu_key.shift = apps_config.get(s_executable_path, "MenuKeyShift", true).as<bool>();
		_screenshot_key.keycode = apps_config.get(s_executable_path, "ScreenshotKey", 0x2C).as<int>(); // VK_SNAPSHOT
		_screenshot_key.ctrl = apps_config.get(s_executable_path, "ScreenshotKeyCtrl", false).as<bool>();
		_screenshot_key.shift = apps_config.get(s_executable_path, "ScreenshotKeyShift", false).as<bool>();
		_screenshot_path = apps_config.get(s_executable_path, "ScreenshotPath", static_cast<const std::string &>(s_executable_path.parent_path())).as<std::string>();
		_screenshot_format = apps_config.get(s_executable_path, "ScreenshotFormat", 0).as<int>();
		_effect_search_paths = apps_config.get(s_executable_path, "EffectSearchPaths", static_cast<const std::string &>(s_injector_path.parent_path())).data();
		_texture_search_paths = apps_config.get(s_executable_path, "TextureSearchPaths", static_cast<const std::string &>(s_injector_path.parent_path())).data();
		_preset_files = apps_config.get(s_executable_path, "Presets", { }).data();

		auto &style = ImGui::GetStyle();

		style.Alpha = style_config.get("Vars", "GlobalAlpha", style.Alpha).as<float>();
		style.WindowPadding = style_config.get("Vars", "WindowPadding", style.WindowPadding).as<ImVec2>();
		style.WindowRounding = style_config.get("Vars", "WindowRounding", 0.0f).as<float>();
		style.ChildWindowRounding = style_config.get("Vars", "ChildWindowRounding", style.ChildWindowRounding).as<float>();
		style.FramePadding = style_config.get("Vars", "FramePadding", style.FramePadding).as<ImVec2>();
		style.FrameRounding = style_config.get("Vars", "FrameRounding", style.FrameRounding).as<float>();
		style.ItemSpacing = style_config.get("Vars", "ItemSpacing", style.ItemSpacing).as<ImVec2>();
		style.ItemInnerSpacing = style_config.get("Vars", "ItemInnerSpacing", style.ItemInnerSpacing).as<ImVec2>();
		style.TouchExtraPadding = style_config.get("Vars", "TouchExtraPadding", style.TouchExtraPadding).as<ImVec2>();
		style.IndentSpacing = style_config.get("Vars", "IndentSpacing", style.IndentSpacing).as<float>();
		style.ScrollbarSize = style_config.get("Vars", "ScrollbarSize", style.ScrollbarSize).as<float>();
		style.ScrollbarRounding = style_config.get("Vars", "ScrollbarRounding", 0.0f).as<float>();
		style.GrabMinSize = style_config.get("Vars", "GrabMinSize", style.GrabMinSize).as<float>();
		style.GrabRounding = style_config.get("Vars", "GrabRounding", style.GrabRounding).as<float>();

		style.Colors[ImGuiCol_WindowBg].w = 0.8f;

		for (unsigned int i = 0; i < ImGuiCol_COUNT; i++)
		{
			style.Colors[i] = style_config.get("Colors", ImGui::GetStyleColName(i), style.Colors[i]).as<ImVec4>();
		}
	}
	void runtime::save_configuration() const
	{
		utils::ini_file apps_config(s_appdata_path / "Apps.ini");
		utils::ini_file style_config(s_appdata_path / "Style.ini");

		apps_config.set(s_executable_path, "DeveloperMode", _developer_mode);
		apps_config.set(s_executable_path, "MenuKey", _menu_key.keycode);
		apps_config.set(s_executable_path, "MenuKeyCtrl", _menu_key.ctrl);
		apps_config.set(s_executable_path, "MenuKeyShift", _menu_key.shift);
		apps_config.set(s_executable_path, "ScreenshotKey", _screenshot_key.keycode);
		apps_config.set(s_executable_path, "ScreenshotKeyCtrl", _screenshot_key.ctrl);
		apps_config.set(s_executable_path, "ScreenshotKeyShift", _screenshot_key.shift);
		apps_config.set(s_executable_path, "ScreenshotPath", _screenshot_path);
		apps_config.set(s_executable_path, "ScreenshotFormat", _screenshot_format);
		apps_config.set(s_executable_path, "EffectSearchPaths", _effect_search_paths);
		apps_config.set(s_executable_path, "TextureSearchPaths", _texture_search_paths);
		apps_config.set(s_executable_path, "Presets", _preset_files);

		const auto &style = ImGui::GetStyle();

		style_config.set("Vars", "GlobalAlpha", style.Alpha);
		style_config.set("Vars", "WindowPadding", style.WindowPadding);
		style_config.set("Vars", "WindowRounding", style.WindowRounding);
		style_config.set("Vars", "ChildWindowRounding", style.ChildWindowRounding);
		style_config.set("Vars", "FramePadding", style.FramePadding);
		style_config.set("Vars", "FrameRounding", style.FrameRounding);
		style_config.set("Vars", "ItemSpacing", style.ItemSpacing);
		style_config.set("Vars", "ItemInnerSpacing", style.ItemInnerSpacing);
		style_config.set("Vars", "TouchExtraPadding", style.TouchExtraPadding);
		style_config.set("Vars", "IndentSpacing", style.IndentSpacing);
		style_config.set("Vars", "ScrollbarSize", style.ScrollbarSize);
		style_config.set("Vars", "ScrollbarRounding", style.ScrollbarRounding);
		style_config.set("Vars", "GrabMinSize", style.GrabMinSize);
		style_config.set("Vars", "GrabRounding", style.GrabRounding);

		for (unsigned int i = 0; i < ImGuiCol_COUNT; i++)
		{
			style_config.set("Colors", ImGui::GetStyleColName(i), style.Colors[i]);
		}
	}
	void runtime::load_preset(const filesystem::path &path)
	{
		utils::ini_file preset(path);

		for (auto &variable : _uniforms)
		{
			const std::string filename = filesystem::path(variable.annotations.at("__FILE__").as<std::string>()).filename();

			float values[4] = { };
			get_uniform_value(variable, values, variable.rows);

			const auto data = preset.get(filename, variable.unique_name, annotation(values, variable.rows));

			for (unsigned int i = 0; i < std::min(variable.rows, static_cast<unsigned int>(data.data().size())); i++)
			{
				values[i] = data.as<float>(i);
			}

			set_uniform_value(variable, values, variable.rows);
		}

		// Reorder techniques
		auto order = preset.get("GLOBAL", "Techniques").data();
		std::sort(_techniques.begin(), _techniques.end(),
			[&order](const technique &lhs, const technique &rhs)
		{
			return (std::find(order.begin(), order.end(), lhs.name) - order.begin()) < (std::find(order.begin(), order.end(), rhs.name) - order.begin());
		});
		for (auto &technique : _techniques)
		{
			technique.enabled = std::find(order.begin(), order.end(), technique.name) != order.end();
		}
	}
	void runtime::save_preset(const filesystem::path &path) const
	{
		utils::ini_file preset(path);

		for (const auto &variable : _uniforms)
		{
			if (variable.annotations.count("source"))
			{
				continue;
			}

			const std::string filename = filesystem::path(variable.annotations.at("__FILE__").as<std::string>()).filename();

			float values[4] = { };
			get_uniform_value(variable, values, variable.rows);

			preset.set(filename, variable.unique_name, annotation(values, variable.rows));
		}

		std::string technique_list;

		for (const auto &technique : _techniques)
		{
			technique_list += technique.name + ',';
		}

		preset.set("GLOBAL", "Techniques", technique_list);
	}

	void runtime::draw_overlay()
	{
		const bool show_splash = std::chrono::duration_cast<std::chrono::seconds>(_last_present - _start_time).count() < 8;

		if ((_menu_index != 1 || !_show_menu) && _input->is_key_pressed(_menu_key.keycode, _menu_key.ctrl, _menu_key.shift, false))
		{
			_show_menu = !_show_menu;
		}

		if (!_show_menu && !show_splash)
		{
			_input->block_mouse_input(false);
			_input->block_keyboard_input(false);
			return;
		}

		auto &imgui_io = ImGui::GetIO();
		imgui_io.DeltaTime = _last_frame_duration.count() * 1e-9f;
		imgui_io.DisplaySize.x = static_cast<float>(_width);
		imgui_io.DisplaySize.y = static_cast<float>(_height);
		imgui_io.Fonts->TexID = _imgui_font_atlas.get();
		imgui_io.MouseDrawCursor = _show_menu;

		imgui_io.MousePos.x = static_cast<float>(_input->mouse_position_x());
		imgui_io.MousePos.y = static_cast<float>(_input->mouse_position_y());
		imgui_io.MouseWheel += _input->mouse_wheel_delta();
		imgui_io.KeyCtrl = _input->is_key_down(0x11); // VK_CONTROL
		imgui_io.KeyShift = _input->is_key_down(0x10); // VK_SHIFT
		imgui_io.KeyAlt = _input->is_key_down(0x12); // VK_MENU

		for (unsigned int i = 0; i < 5; i++)
		{
			imgui_io.MouseDown[i] = _input->is_mouse_button_down(i);
		}
		for (unsigned int i = 0; i < 256; i++)
		{
			imgui_io.KeysDown[i] = _input->is_key_down(i);

			if (!_input->is_key_pressed(i))
			{
				continue;
			}

			imgui_io.AddInputCharacter(_input->key_to_text(i));
		}

		ImGui::NewFrame();

		if (show_splash)
		{
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::Begin("##logo", nullptr, ImVec2(), 0.0f, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
			ImGui::TextColored(ImVec4(0.73f, 0.73f, 0.73f, 1.0f), "ReShade " VERSION_STRING_FILE " by crosire");
			ImGui::TextColored(ImVec4(0.73f, 0.73f, 0.73f, 1.0f), "Visit http://reshade.me for news, updates, shaders and discussion.");
			ImGui::End();
		}

		g_blockSetCursorPos = _show_menu;

		if (_show_menu)
		{
			ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiSetCond_FirstUseEver);
			ImGui::SetNextWindowPosCenter(ImGuiSetCond_FirstUseEver);
			ImGui::Begin("ReShade " VERSION_STRING_PRODUCT " by crosire", &_show_menu, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse);

			draw_overlay_menu();

			ImGui::End();
		}

		if (_developer_mode)
		{
			ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiSetCond_FirstUseEver);

			if (ImGui::Begin("Effect Compiler Log"))
			{
				ImGui::TextColored(ImVec4(1, 0, 0, 1), _errors.c_str());
			}

			ImGui::End();
		}

		if (_show_shader_editor)
		{
			ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiSetCond_FirstUseEver);

			if (ImGui::Begin("Shader Code", &_show_shader_editor))
			{
				draw_overlay_shader_editor();
			}

			ImGui::End();
		}
		if (_show_variable_editor)
		{
			ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiSetCond_FirstUseEver);

			if (ImGui::Begin("Shaders Parameters", &_show_variable_editor))
			{
				draw_overlay_variable_editor();
			}

			ImGui::End();
		}

		ImGui::Render();

		_input->block_mouse_input(imgui_io.WantCaptureMouse);
		_input->block_keyboard_input(imgui_io.WantCaptureKeyboard);

		render_draw_lists(ImGui::GetDrawData());
	}
	void runtime::draw_overlay_menu()
	{
		if (ImGui::BeginMenuBar())
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImGui::GetStyle().ItemSpacing * 2);

			const char *menu_items[] = { "Home", "Settings", "Statistics", "About" };

			for (int i = 0; i < 4; i++)
			{
				if (ImGui::Selectable(menu_items[i], _menu_index == i, 0, ImVec2(ImGui::CalcTextSize(menu_items[i]).x, 0)))
				{
					_menu_index = i;
				}

				ImGui::SameLine();
			}

			ImGui::PopStyleVar();

			ImGui::EndMenuBar();
		}

		switch (_menu_index)
		{
			case 0:
				draw_overlay_menu_home();
				break;
			case 1:
				draw_overlay_menu_settings();
				break;
			case 2:
				draw_overlay_menu_statistics();
				break;
			case 3:
				draw_overlay_menu_about();
				break;
		}
	}
	void runtime::draw_overlay_menu_home()
	{
		if (_developer_mode)
		{
			if (ImGui::Button("Reload", ImVec2(-1, 0)))
			{
				reload();
			}

			if (ImGui::Button("Open Shader Editor", ImVec2(-1, 0)))
			{
				_show_shader_editor = true;
			}
		}

		std::string preset_files;
		for (const std::string &path : _preset_files)
		{
			preset_files += path + '\0';
		}

		ImGui::PushItemWidth(-(60 + ImGui::GetStyle().ItemSpacing.x) * 3 - 1);

		if (ImGui::Combo("##presets", &_current_preset, preset_files.c_str()))
		{
			load_preset(_preset_files[_current_preset]);
		}

		if (_current_preset >= 0 && ImGui::IsItemHovered() && !_input->is_mouse_button_down(0))
		{
			ImGui::SetTooltip(static_cast<const std::string &>(_preset_files[_current_preset]).c_str());
		}

		ImGui::SameLine();

		if (ImGui::Button("Load", ImVec2(60, 0)))
		{
			ImGui::OpenPopup("Add Preset");
		}

		if (_current_preset >= 0)
		{
			ImGui::SameLine();

			if (ImGui::ButtonEx("Remove", ImVec2(60, 0)))
			{
				ImGui::OpenPopup("Remove Preset");
			}

			ImGui::SameLine();

			if (ImGui::ButtonEx("Edit", ImVec2(60, 0)))
			{
				_show_variable_editor = true;
			}
		}

		if (ImGui::BeginPopup("Add Preset"))
		{
			char buf[260] = { };

			if (ImGui::InputText("Path to INI", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				const auto path = filesystem::absolute(buf);

				if (filesystem::exists(path) || filesystem::exists(path.parent_path()))
				{
					_preset_files.push_back(path);

					save_configuration();

					ImGui::CloseCurrentPopup();
				}
				else
				{

				}
			}

			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("Remove Preset"))
		{
			ImGui::Text("Do you really want to remove this preset?");

			if (ImGui::Button("Yes", ImVec2(-1, 0)))
			{
				_preset_files.erase(_preset_files.begin() + _current_preset);
				_current_preset = -1;

				save_configuration();

				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		ImGui::Spacing();

		if (ImGui::BeginChild("##techniques", ImVec2(-1, -1), true, 0))
		{
			for (size_t n = 0; n < _techniques.size(); n++)
			{
				if (_techniques[n].annotations["hidden"].as<bool>())
				{
					continue;
				}

				ImGui::PushID(n);

				const auto name = _techniques[n].name + " [" + _techniques[n].annotations["__FILE__"].as<std::string>() + "]";

				if (ImGui::Checkbox(name.c_str(), &_techniques[n].enabled) && _current_preset >= 0)
				{
					save_preset(_preset_files[_current_preset]);
				}

				if (ImGui::IsItemActive())
				{
					_selected_technique = n;
				}
				if (ImGui::IsItemHoveredRect())
				{
					_hovered_technique = n;
				}

				ImGui::PopID();
			}
		}

		ImGui::EndChild();

		if (!_developer_mode && ImGui::IsItemHovered() && !_input->is_mouse_button_down(0))
		{
			ImGui::SetTooltip("Click on a technique to enable/disable it.\nClick and then drag one to a new location in the list to change the execution order.");
		}

		if (ImGui::IsMouseDragging() && _selected_technique >= 0)
		{
			ImGui::SetTooltip(_techniques[_selected_technique].name.c_str());

			if (_hovered_technique >= 0 && _hovered_technique != _selected_technique)
			{
				std::swap(_techniques[_hovered_technique], _techniques[_selected_technique]);
				_selected_technique = _hovered_technique;

				if (_current_preset >= 0)
				{
					save_preset(_preset_files[_current_preset]);
				}
			}
		}
		else
		{
			_selected_technique = _hovered_technique = -1;
		}
	}
	void runtime::draw_overlay_menu_settings()
	{
		char edit_buffer[2048];

		if (ImGui::Button("Save", ImVec2(-1, 0)))
		{
			save_configuration();
		}

		ImGui::Spacing();

		if (ImGui::CollapsingHeader("General", "settings_general", true, true))
		{
			assert(_menu_key.keycode < 256);

			std::string menu_key_text = keyboard_keys[_menu_key.keycode];
			if (_menu_key.shift) menu_key_text = "Shift + " + menu_key_text;
			if (_menu_key.ctrl) menu_key_text = "Ctrl + " + menu_key_text;
			ImGui::InputText("Overlay Key", const_cast<char *>(menu_key_text.data()), menu_key_text.size(), ImGuiInputTextFlags_ReadOnly);

			if (ImGui::IsItemActive())
			{
				if (_input->is_any_key_pressed())
				{
					const unsigned int last_key_pressed = _input->last_key_pressed();

					if (last_key_pressed != 0x11 && last_key_pressed != 0x10)
					{
						_menu_key.ctrl = _input->is_key_down(0x11);
						_menu_key.shift = _input->is_key_down(0x10);
						_menu_key.keycode = _input->last_key_pressed();
					}
				}
			}
			else if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Click in the field and press any key to change the shortcut to that key.");
			}

			int overlay_mode_index = _developer_mode ? 1 : 0;

			if (ImGui::Combo("Overlay Mode", &overlay_mode_index, "User Mode\0Developer Mode\0"))
			{
				_developer_mode = overlay_mode_index != 0;
			}

			size_t offset = 0;
			for (const std::string &search_path : _effect_search_paths)
			{
				memcpy(edit_buffer + offset, search_path.c_str(), search_path.size());
				offset += search_path.size();
				edit_buffer[offset++] = '\n';
				edit_buffer[offset] = '\0';
			}

			if (ImGui::InputTextMultiline("Effect Search Paths", edit_buffer, sizeof(edit_buffer), ImVec2(0, 100)))
			{
				_effect_search_paths.clear();

				for (const auto &search_path : stdext::split(edit_buffer, '\n'))
				{
					_effect_search_paths.push_back(search_path);
				}
			}

			offset = 0;
			for (const std::string &search_path : _texture_search_paths)
			{
				memcpy(edit_buffer + offset, search_path.c_str(), search_path.size());
				offset += search_path.size();
				edit_buffer[offset++] = '\n';
				edit_buffer[offset] = '\0';
			}

			if (ImGui::InputTextMultiline("Texture Search Paths", edit_buffer, sizeof(edit_buffer), ImVec2(0, 100)))
			{
				_texture_search_paths.clear();

				for (const auto &search_path : stdext::split(edit_buffer, '\n'))
				{
					_texture_search_paths.push_back(search_path);
				}
			}
		}

		if (ImGui::CollapsingHeader("Screenshots", "settings_screenshots", true, true))
		{
			assert(_screenshot_key.keycode < 256);

			std::string screenshot_key_text = keyboard_keys[_screenshot_key.keycode];
			if (_screenshot_key.shift) screenshot_key_text = "Shift + " + screenshot_key_text;
			if (_screenshot_key.ctrl) screenshot_key_text = "Ctrl + " + screenshot_key_text;
			ImGui::InputText("Screenshot Key", const_cast<char *>(screenshot_key_text.data()), screenshot_key_text.size(), ImGuiInputTextFlags_ReadOnly);

			if (ImGui::IsItemActive())
			{
				if (_input->is_any_key_pressed())
				{
					const unsigned int last_key_pressed = _input->last_key_pressed();

					if (last_key_pressed != 0x11 && last_key_pressed != 0x10)
					{
						_screenshot_key.ctrl = _input->is_key_down(0x11);
						_screenshot_key.shift = _input->is_key_down(0x10);
						_screenshot_key.keycode = _input->last_key_pressed();
					}
				}
			}
			else if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Click in the field and press any key to change the shortcut to that key.");
			}

			memcpy(edit_buffer, _screenshot_path.c_str(), _screenshot_path.size() + 1);

			if (ImGui::InputText("Screenshot Path", edit_buffer, sizeof(edit_buffer)))
			{
				_screenshot_path = edit_buffer;
			}

			ImGui::Combo("Screenshot Format", &_screenshot_format, "Bitmap (*.bmp)\0Portable Network Graphics (*.png)\0");
		}

		if (ImGui::CollapsingHeader("User Interface", "settings_ui", true, true))
		{
			auto &style = ImGui::GetStyle();

			ImGui::DragFloat("GlobalAlpha", &style.Alpha, 0.005f, 0.20f, 1.0f, "%.2f");
			ImGui::SliderFloat2("WindowPadding", &style.WindowPadding.x, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0f, 16.0f, "%.0f");
			ImGui::SliderFloat("ChildWindowRounding", &style.ChildWindowRounding, 0.0f, 16.0f, "%.0f");
			ImGui::SliderFloat2("FramePadding", &style.FramePadding.x, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat("FrameRounding", &style.FrameRounding, 0.0f, 16.0f, "%.0f");
			ImGui::SliderFloat2("ItemSpacing", &style.ItemSpacing.x, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat2("ItemInnerSpacing", &style.ItemInnerSpacing.x, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat2("TouchExtraPadding", &style.TouchExtraPadding.x, 0.0f, 10.0f, "%.0f");
			ImGui::SliderFloat("IndentSpacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
			ImGui::SliderFloat("ScrollbarSize", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
			ImGui::SliderFloat("ScrollbarRounding", &style.ScrollbarRounding, 0.0f, 16.0f, "%.0f");
			ImGui::SliderFloat("GrabMinSize", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");
			ImGui::SliderFloat("GrabRounding", &style.GrabRounding, 0.0f, 16.0f, "%.0f");

			for (int i = 0; i < ImGuiCol_COUNT; i++)
			{
				ImGui::PushID(i);
				ImGui::ColorEdit4(ImGui::GetStyleColName(i), &style.Colors[i].x, true);
				ImGui::PopID();
			}
		}
	}
	void runtime::draw_overlay_menu_statistics()
	{
		const auto state = static_cast<ImGuiState *>(ImGui::GetInternalState());

		if (ImGui::CollapsingHeader("General", "statistics_general", true, true))
		{
			ImGui::Text("Application: %X", std::hash<std::string>()(s_executable_name));
			ImGui::Text("Date: %d-%d-%d %d", _date[0], _date[1], _date[2], _date[3]);
			ImGui::Text("Device: %X %d", _vendor_id, _device_id);
			ImGui::Text("FPS: %.2f", ImGui::GetIO().Framerate);
			ImGui::PushItemWidth(-1);
			ImGui::PlotLines("##framerate", state->FramerateSecPerFrame, 120, state->FramerateSecPerFrameIdx, nullptr, state->FramerateSecPerFrameAccum / 120 * 0.5f, state->FramerateSecPerFrameAccum / 120 * 1.5f, ImVec2(0, 50));
			ImGui::Text("Draw Calls: %u (%u vertices)", _drawcalls, _vertices);
			ImGui::Text("Frame %llu: %fms", _framecount + 1, _last_frame_duration.count() * 1e-6f);
			ImGui::Text("Timer: %fms", std::fmod(std::chrono::duration_cast<std::chrono::nanoseconds>(_last_present - _start_time).count() * 1e-6f, 16777216.0f));
			ImGui::Text("Network: %uB", g_network_traffic);
		}

		if (_developer_mode)
		{
			if (ImGui::CollapsingHeader("Textures", "statistics_textures", true, true))
			{
				for (const auto &texture : _textures)
				{
					ImGui::Text("%s: %ux%u+%u (%uB)", texture->name.c_str(), texture->width, texture->height, (texture->levels - 1), static_cast<unsigned int>(texture->storage_size));
				}
			}
			if (ImGui::CollapsingHeader("Techniques", "statistics_techniques", true, true))
			{
				for (const auto &technique : _techniques)
				{
					ImGui::Text("%s (%u passes): %fms", technique.name.c_str(), static_cast<unsigned int>(technique.passes.size()), (technique.average_duration * 1e-6f));
				}
			}
		}
	}
	void runtime::draw_overlay_menu_about()
	{
		ImGui::TextWrapped("\
Copyright (C) 2014 Patrick \"crosire\" Mours\n\
\n\
This software is provided 'as-is', without any express or implied warranty.\n\
In no event will the authors be held liable for any damages arising from the use of this software.\n\
\n\
Libraries in use:\n\
- MinHook\n\
  Tsuda Kageyu and contributors\n\
- gl3w\n\
  Slavomir Kaslev\n\
- dear imgui\n\
  Omar Cornut and contributors\n\
- stb_image\n\
  Sean Barrett and contributors\n\
- DDS loading from SOIL\n\
  Jonathan \"lonesock\" Dummer");
	}
	void runtime::draw_overlay_shader_editor()
	{
		std::string effect_files;
		for (const std::string &path : _included_files)
		{
			effect_files += path + '\0';
		}

		ImGui::PushItemWidth(-1);
		if (ImGui::Combo("##effect_files", &_current_effect_file, effect_files.c_str()))
		{
			std::ifstream file(stdext::utf8_to_utf16(_included_files[_current_effect_file]).c_str());

			if (file.is_open())
			{
				const auto file_size = stream_size(file);

				if (file_size > _shader_edit_buffer.size())
				{
					_shader_edit_buffer.resize(file_size);
				}

				std::copy(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), _shader_edit_buffer.begin());
			}
			else
			{
			}
		}

		ImGui::Separator();

		if (!_errors.empty())
		{
			ImGui::BeginChildFrame(0, ImVec2(-1, 80));
			ImGui::TextColored(ImVec4(1, 0, 0, 1), _errors.c_str());
			ImGui::EndChildFrame();
		}

		ImGui::InputTextMultiline("##editor", _shader_edit_buffer.data(), _shader_edit_buffer.size(), ImVec2(-1, -20), ImGuiInputTextFlags_AllowTabInput);

		if (ImGui::Button("Save", ImVec2(-1, 0)) || _input->is_key_pressed('S', true, false, false))
		{
			std::ofstream file(stdext::utf8_to_utf16(_included_files[_current_effect_file]).c_str(), std::ios::out | std::ios::trunc);
			file << _shader_edit_buffer.data();
			file.close();

			reload();
		}
	}
	void runtime::draw_overlay_variable_editor()
	{
		bool opened = true;
		size_t id = 0;
		std::string current_header, new_header;

		for (auto &variable : _uniforms)
		{
			if (variable.annotations.count("source"))
			{
				continue;
			}

			new_header = variable.annotations["__FILE__"].as<std::string>();

			if (new_header != current_header)
			{
				current_header = new_header;
				opened = ImGui::CollapsingHeader(new_header.c_str(), nullptr, true, true);
			}
			if (!opened)
			{
				continue;
			}

			bool modified = false;
			float data[4] = { };
			get_uniform_value(variable, data, 4);

			auto ui_type = variable.annotations["ui_type"].as<std::string>();

			ImGui::PushID(id++);

			if (ui_type == "drag")
			{
				switch (variable.rows)
				{
					case 1:
						modified = ImGui::DragFloat(variable.name.c_str(), data, variable.annotations["ui_step"].as<float>(), variable.annotations["ui_min"].as<float>(), variable.annotations["ui_max"].as<float>());
						break;
					case 2:
						modified = ImGui::DragFloat2(variable.name.c_str(), data, variable.annotations["ui_step"].as<float>(), variable.annotations["ui_min"].as<float>(), variable.annotations["ui_max"].as<float>());
						break;
					case 3:
						modified = ImGui::DragFloat3(variable.name.c_str(), data, variable.annotations["ui_step"].as<float>(), variable.annotations["ui_min"].as<float>(), variable.annotations["ui_max"].as<float>());
						break;
					case 4:
						modified = ImGui::DragFloat4(variable.name.c_str(), data, variable.annotations["ui_step"].as<float>(), variable.annotations["ui_min"].as<float>(), variable.annotations["ui_max"].as<float>());
						break;
				}
			}
			else if (ui_type == "input" || (ui_type.empty() && variable.rows < 3))
			{
				switch (variable.rows)
				{
					case 1:
						modified = ImGui::InputFloat(variable.name.c_str(), data);
						break;
					case 2:
						modified = ImGui::InputFloat2(variable.name.c_str(), data);
						break;
					case 3:
						modified = ImGui::InputFloat3(variable.name.c_str(), data);
						break;
					case 4:
						modified = ImGui::InputFloat4(variable.name.c_str(), data);
						break;
				}
			}
			else if (variable.rows == 3)
			{
				modified = ImGui::ColorEdit3(variable.name.c_str(), data);
			}
			else if (variable.rows == 4)
			{
				modified = ImGui::ColorEdit4(variable.name.c_str(), data);
			}

			ImGui::PopID();

			if (modified)
			{
				set_uniform_value(variable, data, 4);

				if (_current_preset >= 0)
				{
					save_preset(_preset_files[_current_preset]);
				}
			}
		}
	}
}