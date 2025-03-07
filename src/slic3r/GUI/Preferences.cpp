#include "Preferences.hpp"
#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include <wx/notebook.h>
#include "Notebook.hpp"
#include "ButtonsDescription.hpp"
#include "OG_CustomCtrl.hpp"
#include "GLCanvas3D.hpp"
#include "ConfigWizard_private.hpp"

#include <boost/dll/runtime_symbol_info.hpp>

#ifdef WIN32
#include <wx/msw/registry.h>
#endif // WIN32
#ifdef __linux__
#include "DesktopIntegrationDialog.hpp"
#endif //__linux__

namespace Slic3r {

	static t_config_enum_names enum_names_from_keys_map(const t_config_enum_values& enum_keys_map)
	{
		t_config_enum_names names;
		int cnt = 0;
		for (const auto& kvp : enum_keys_map)
			cnt = std::max(cnt, kvp.second);
		cnt += 1;
		names.assign(cnt, "");
		for (const auto& kvp : enum_keys_map)
			names[kvp.second] = kvp.first;
		return names;
	}

#define CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NAME) \
    static t_config_enum_names s_keys_names_##NAME = enum_names_from_keys_map(s_keys_map_##NAME); \
    template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values() { return s_keys_map_##NAME; } \
    template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names() { return s_keys_names_##NAME; }



	static const t_config_enum_values s_keys_map_NotifyReleaseMode = {
		{"all",         NotifyReleaseAll},
		{"release",     NotifyReleaseOnly},
		{"none",        NotifyReleaseNone},
	};

	CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NotifyReleaseMode)

namespace GUI {

PreferencesDialog::PreferencesDialog(wxWindow* parent) :
    DPIDialog(parent, wxID_ANY, _L("Preferences"), wxDefaultPosition, 
              wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
#ifdef __WXOSX__
    isOSX = true;
#endif
	build();

	m_highlighter.set_timer_owner(this, 0);
}

static void update_color(wxColourPickerCtrl* color_pckr, const wxColour& color) 
{
	if (color_pckr->GetColour() != color) {
		color_pckr->SetColour(color);
		wxPostEvent(color_pckr, wxCommandEvent(wxEVT_COLOURPICKER_CHANGED));
	}
}

void PreferencesDialog::show(const std::string& highlight_opt_key /*= std::string()*/, const std::string& tab_name/*= std::string()*/)
{
	int selected_tab = 0;
	for ( ; selected_tab < int(tabs->GetPageCount()); selected_tab++)
		if (tabs->GetPageText(selected_tab) == _(tab_name))
			break;
	if (selected_tab < int(tabs->GetPageCount()))
		tabs->SetSelection(selected_tab);

	if (!highlight_opt_key.empty())
		init_highlighter(highlight_opt_key);

	// cache input values for custom toolbar size
	m_custom_toolbar_size		= atoi(get_app_config()->get("custom_toolbar_size").c_str());
	m_use_custom_toolbar_size	= get_app_config()->get("use_custom_toolbar_size") == "1";

	// set Field for notify_release to its value
	if (m_optgroup_gui && m_optgroup_gui->get_field("notify_release") != nullptr) {
		boost::any val = s_keys_map_NotifyReleaseMode.at(wxGetApp().app_config->get("notify_release"));
		m_optgroup_gui->get_field("notify_release")->set_value(val, false);
	}
	

	if (wxGetApp().is_editor()) {
		auto app_config = get_app_config();

		downloader->set_path_name(app_config->get("url_downloader_dest"));
		downloader->allow(!app_config->has("downloader_url_registered") || app_config->get("downloader_url_registered") == "1");

		for (const std::string& opt_key : {"suppress_hyperlinks", "downloader_url_registered"})
			m_optgroup_other->set_value(opt_key, app_config->get(opt_key) == "1");

		// update colors for color pickers of the labels
		update_color(m_sys_colour, wxGetApp().get_label_clr_sys());
		update_color(m_mod_colour, wxGetApp().get_label_clr_modified());

		// update color pickers for mode palette
		const auto palette = wxGetApp().get_mode_palette(); 
		std::vector<wxColourPickerCtrl*> color_pickres = {m_mode_simple, m_mode_advanced, m_mode_expert};
		for (size_t mode = 0; mode < color_pickres.size(); ++mode)
			update_color(color_pickres[mode], palette[mode]);
	}

	this->ShowModal();
}

static std::shared_ptr<ConfigOptionsGroup>create_options_tab(const wxString& title, wxBookCtrlBase* tabs)
{
	wxPanel* tab = new wxPanel(tabs, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
	tabs->AddPage(tab, _(title));
	tab->SetFont(wxGetApp().normal_font());

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->SetSizeHints(tab);
	tab->SetSizer(sizer);

	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(tab);
	optgroup->label_width = 40;
	optgroup->set_config_category_and_type(title, int(Preset::TYPE_PREFERENCES));
	return optgroup;
}

static void activate_options_tab(std::shared_ptr<ConfigOptionsGroup> optgroup)
{
	optgroup->activate([](){}, wxALIGN_RIGHT);
	optgroup->update_visibility(comSimple);
	wxBoxSizer* sizer = static_cast<wxBoxSizer*>(static_cast<wxPanel*>(optgroup->parent())->GetSizer());
	sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 10);

	// apply sercher
	wxGetApp().sidebar().get_searcher().append_preferences_options(optgroup->get_lines());
}

static void append_bool_option( std::shared_ptr<ConfigOptionsGroup> optgroup,
								const std::string& opt_key,
								const std::string& label,
								const std::string& tooltip,
								bool def_val,
								ConfigOptionMode mode = comSimple)
{
	ConfigOptionDef def = {opt_key, coBool};
	def.label = label;
	def.tooltip = tooltip;
	def.mode = mode;
	def.set_default_value(new ConfigOptionBool{ def_val });
	Option option(def, opt_key);
	optgroup->append_single_option_line(option);

	// fill data to the Search Dialog
	wxGetApp().sidebar().get_searcher().add_key(opt_key, Preset::TYPE_PREFERENCES, optgroup->config_category(), L("Preferences"));
}

static void append_enum_option( std::shared_ptr<ConfigOptionsGroup> optgroup,
								const std::string& opt_key,
								const std::string& label,
								const std::string& tooltip,
								const ConfigOption* def_val,
								const t_config_enum_values *enum_keys_map,
								std::initializer_list<std::string> enum_values,
								std::initializer_list<std::string> enum_labels,
								ConfigOptionMode mode = comSimple)
{
	ConfigOptionDef def = {opt_key, coEnum };
	def.label = label;
	def.tooltip = tooltip;
	def.mode = mode;
	def.enum_keys_map = enum_keys_map;
	def.enum_values = std::vector<std::string>(enum_values);
	def.enum_labels = std::vector<std::string>(enum_labels);

	def.set_default_value(def_val);
	Option option(def, opt_key);
	optgroup->append_single_option_line(option);

	// fill data to the Search Dialog
	wxGetApp().sidebar().get_searcher().add_key(opt_key, Preset::TYPE_PREFERENCES, optgroup->config_category(), L("Preferences"));
}

static void append_string_option(std::shared_ptr<ConfigOptionsGroup> optgroup,
									const std::string& opt_key,
									const std::string& label,
									const std::string& tooltip,
									const std::string& def_val,
									ConfigOptionMode mode = comSimple)
{
	ConfigOptionDef def = { opt_key, coString };
	def.label = label;
	def.tooltip = tooltip;
	def.mode = mode;
	def.set_default_value(new ConfigOptionString{ def_val });
	Option option(def, opt_key);
	optgroup->append_single_option_line(option);

	// fill data to the Search Dialog
	wxGetApp().sidebar().get_searcher().add_key(opt_key, Preset::TYPE_PREFERENCES, optgroup->config_category(), L("Preferences"));
}

static void append_preferences_option_to_searcer(std::shared_ptr<ConfigOptionsGroup> optgroup,
												const std::string& opt_key,
												const wxString& label)
{
	// fill data to the Search Dialog
	wxGetApp().sidebar().get_searcher().add_key(opt_key, Preset::TYPE_PREFERENCES, optgroup->config_category(), L("Preferences"));
	// apply sercher
	wxGetApp().sidebar().get_searcher().append_preferences_option(Line(opt_key, label, ""));
}

void PreferencesDialog::build()
{
#ifdef _WIN32
	wxGetApp().UpdateDarkUI(this);
#else
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
	const wxFont& font = wxGetApp().normal_font();
	SetFont(font);

	auto app_config = get_app_config();

#ifdef _MSW_DARK_MODE
	tabs = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME | wxNB_DEFAULT);
#else
    tabs = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL  |wxNB_NOPAGETHEME | wxNB_DEFAULT );
	tabs->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

	// Add "General" tab
	m_optgroup_general = create_options_tab(L("General"), tabs);
	m_optgroup_general->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
		if (auto it = m_values.find(opt_key); it != m_values.end()) {
			m_values.erase(it); // we shouldn't change value, if some of those parameters were selected, and then deselected
			return;
		}
		if (opt_key == "default_action_on_close_application" || opt_key == "default_action_on_select_preset" || opt_key == "default_action_on_new_project")
			m_values[opt_key] = boost::any_cast<bool>(value) ? "none" : "discard";
		else if (opt_key == "default_action_on_dirty_project")
			m_values[opt_key] = boost::any_cast<bool>(value) ? "" : "0";
		else
		    m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
	};

	bool is_editor = wxGetApp().is_editor();

	if (is_editor) {
		append_bool_option(m_optgroup_general, "remember_output_path", 
			L("Remember output directory"),
			L("If this is enabled, Slic3r will prompt the last output directory instead of the one containing the input files."),
			app_config->has("remember_output_path") ? app_config->get("remember_output_path") == "1" : true);

		append_bool_option(m_optgroup_general, "autocenter", 
			L("Auto-center parts"),
			L("If this is enabled, Slic3r will auto-center objects around the print bed center."),
			app_config->get("autocenter") == "1");

		append_bool_option(m_optgroup_general, "background_processing", 
			L("Background processing"),
			L("If this is enabled, Slic3r will pre-process objects as soon "
				"as they\'re loaded in order to save time when exporting G-code."),
			app_config->get("background_processing") == "1");

		append_bool_option(m_optgroup_general, "alert_when_supports_needed", 
			L("Alert when supports needed"),
			L("If this is enabled, Slic3r will raise alerts when it detects "
				"issues in the sliced object, that can be resolved with supports (and brim). "
				"Examples of such issues are floating object parts, unsupported extrusions and low bed adhesion."),
			app_config->get("alert_when_supports_needed") == "1");


		m_optgroup_general->append_separator();

		// Please keep in sync with ConfigWizard
		append_bool_option(m_optgroup_general, "export_sources_full_pathnames",
			L("Export sources full pathnames to 3mf and amf"),
			L("If enabled, allows the Reload from disk command to automatically find and load the files when invoked."),
			app_config->get("export_sources_full_pathnames") == "1");

#ifdef _WIN32
		// Please keep in sync with ConfigWizard
		append_bool_option(m_optgroup_general, "associate_3mf",
			L("Associate .3mf files to PrusaSlicer"),
			L("If enabled, sets PrusaSlicer as default application to open .3mf files."),
			app_config->get("associate_3mf") == "1");

		append_bool_option(m_optgroup_general, "associate_stl",
			L("Associate .stl files to PrusaSlicer"),
			L("If enabled, sets PrusaSlicer as default application to open .stl files."),
			app_config->get("associate_stl") == "1");
#endif // _WIN32

		m_optgroup_general->append_separator();

		// Please keep in sync with ConfigWizard
		append_bool_option(m_optgroup_general, "preset_update",
			L("Update built-in Presets automatically"),
			L("If enabled, Slic3r downloads updates of built-in system presets in the background. These updates are downloaded "
			  "into a separate temporary location. When a new preset version becomes available it is offered at application startup."),
			app_config->get("preset_update") == "1");

		append_bool_option(m_optgroup_general, "no_defaults",
			L("Suppress \" - default - \" presets"),
			L("Suppress \" - default - \" presets in the Print / Filament / Printer selections once there are any other valid presets available."),
			app_config->get("no_defaults") == "1");

		append_bool_option(m_optgroup_general, "no_templates",
			L("Suppress \" Template \" filament presets"),
			L("Suppress \" Template \" filament presets in configuration wizard and sidebar visibility."),
			app_config->get("no_templates") == "1");

		append_bool_option(m_optgroup_general, "show_incompatible_presets",
			L("Show incompatible print and filament presets"),
			L("When checked, the print and filament presets are shown in the preset editor "
			"even if they are marked as incompatible with the active printer"),
			app_config->get("show_incompatible_presets") == "1");

		m_optgroup_general->append_separator();

		append_bool_option(m_optgroup_general, "show_drop_project_dialog",
#if 1 // #ysFIXME_delete_after_test_of_6377
			L("Show load project dialog"),
			L("When checked, whenever dragging and dropping a project file on the application or open it from a browser, "
			  "shows a dialog asking to select the action to take on the file to load."),
#else
			L("Show drop project dialog"),
			L("When checked, whenever dragging and dropping a project file on the application, shows a dialog asking to select the action to take on the file to load."),
#endif
			app_config->get("show_drop_project_dialog") == "1");

		append_bool_option(m_optgroup_general, "single_instance",
#if __APPLE__
			L("Allow just a single PrusaSlicer instance"),
			L("On OSX there is always only one instance of app running by default. However it is allowed to run multiple instances "
			  "of same app from the command line. In such case this settings will allow only one instance."),
#else
			L("Allow just a single PrusaSlicer instance"),
			L("If this is enabled, when starting PrusaSlicer and another instance of the same PrusaSlicer is already running, that instance will be reactivated instead."),
#endif
		app_config->has("single_instance") ? app_config->get("single_instance") == "1" : false );

		m_optgroup_general->append_separator();

		append_bool_option(m_optgroup_general, "default_action_on_dirty_project",
			L("Ask for unsaved changes in project"),
			L("Always ask for unsaved changes in project, when: \n"
						"- Closing PrusaSlicer,\n"
						"- Loading or creating a new project"),
			app_config->get("default_action_on_dirty_project").empty());

		m_optgroup_general->append_separator();

		append_bool_option(m_optgroup_general, "default_action_on_close_application",
			L("Ask to save unsaved changes in presets when closing the application or when loading a new project"),
			L("Always ask for unsaved changes in presets, when: \n"
						"- Closing PrusaSlicer while some presets are modified,\n"
						"- Loading a new project while some presets are modified"),
			app_config->get("default_action_on_close_application") == "none");

		append_bool_option(m_optgroup_general, "default_action_on_select_preset",
			L("Ask for unsaved changes in presets when selecting new preset"),
			L("Always ask for unsaved changes in presets when selecting new preset or resetting a preset"),
			app_config->get("default_action_on_select_preset") == "none");

		append_bool_option(m_optgroup_general, "default_action_on_new_project",
			L("Ask for unsaved changes in presets when creating new project"),
			L("Always ask for unsaved changes in presets when creating new project"),
			app_config->get("default_action_on_new_project") == "none");
	}
#ifdef _WIN32
	else {
		append_bool_option(m_optgroup_general, "associate_gcode",
			L("Associate .gcode files to PrusaSlicer G-code Viewer"),
			L("If enabled, sets PrusaSlicer G-code Viewer as default application to open .gcode files."),
			app_config->get("associate_gcode") == "1");
	}
#endif // _WIN32

#if __APPLE__
	append_bool_option(m_optgroup_general, "use_retina_opengl",
		L("Use Retina resolution for the 3D scene"),
		L("If enabled, the 3D scene will be rendered in Retina resolution. "
	      "If you are experiencing 3D performance problems, disabling this option may help."),
		app_config->get("use_retina_opengl") == "1");
#endif

	m_optgroup_general->append_separator();

    // Show/Hide splash screen
	append_bool_option(m_optgroup_general, "show_splash_screen",
		L("Show splash screen"),
		L("Show splash screen"),
		app_config->get("show_splash_screen") == "1");

	append_bool_option(m_optgroup_general, "restore_win_position",
		L("Restore window position on start"),
		L("If enabled, PrusaSlicer will be open at the position it was closed"),
		app_config->get("restore_win_position") == "1");

    // Clear Undo / Redo stack on new project
	append_bool_option(m_optgroup_general, "clear_undo_redo_stack_on_new_project",
		L("Clear Undo / Redo stack on new project"),
		L("Clear Undo / Redo stack on new project or when an existing project is loaded."),
		app_config->get("clear_undo_redo_stack_on_new_project") == "1");

#if defined(_WIN32) || defined(__APPLE__)
	append_bool_option(m_optgroup_general, "use_legacy_3DConnexion",
		L("Enable support for legacy 3DConnexion devices"),
		L("If enabled, the legacy 3DConnexion devices settings dialog is available by pressing CTRL+M"),
		app_config->get("use_legacy_3DConnexion") == "1");
#endif // _WIN32 || __APPLE__

	activate_options_tab(m_optgroup_general);

	// Add "Camera" tab
	m_optgroup_camera = create_options_tab(L("Camera"), tabs);
	m_optgroup_camera->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
		if (auto it = m_values.find(opt_key);it != m_values.end()) {
			m_values.erase(it); // we shouldn't change value, if some of those parameters were selected, and then deselected
			return;
		}
		m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
	};

	append_bool_option(m_optgroup_camera, "use_perspective_camera",
		L("Use perspective camera"),
		L("If enabled, use perspective camera. If not enabled, use orthographic camera."),
		app_config->get("use_perspective_camera") == "1");

	append_bool_option(m_optgroup_camera, "use_free_camera",
		L("Use free camera"),
		L("If enabled, use free camera. If not enabled, use constrained camera."),
		app_config->get("use_free_camera") == "1");

	append_bool_option(m_optgroup_camera, "reverse_mouse_wheel_zoom",
		L("Reverse direction of zoom with mouse wheel"),
		L("If enabled, reverses the direction of zoom with mouse wheel"),
		app_config->get("reverse_mouse_wheel_zoom") == "1");

	activate_options_tab(m_optgroup_camera);

	// Add "GUI" tab
	m_optgroup_gui = create_options_tab(L("GUI"), tabs);
	m_optgroup_gui->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
		if (opt_key == "notify_release") {
			int val_int = boost::any_cast<int>(value);
			for (const auto& item : s_keys_map_NotifyReleaseMode) {
				if (item.second == val_int) {
					m_values[opt_key] = item.first;
					return;
				}
			}
		}
		if (opt_key == "use_custom_toolbar_size") {
			m_icon_size_sizer->ShowItems(boost::any_cast<bool>(value));
			refresh_og(m_optgroup_gui);
			get_app_config()->set("use_custom_toolbar_size", boost::any_cast<bool>(value) ? "1" : "0");
			get_app_config()->save();
			wxGetApp().plater()->get_current_canvas3D()->render();
			return;
		}
		if (opt_key == "tabs_as_menu") {
			bool disable_new_layout = boost::any_cast<bool>(value);
			m_rb_new_settings_layout_mode->Show(!disable_new_layout);
			if (disable_new_layout && m_rb_new_settings_layout_mode->GetValue()) {
				m_rb_new_settings_layout_mode->SetValue(false);
				m_rb_old_settings_layout_mode->SetValue(true);
			}
			refresh_og(m_optgroup_gui);
		}

		if (auto it = m_values.find(opt_key); it != m_values.end()) {
			m_values.erase(it); // we shouldn't change value, if some of those parameters were selected, and then deselected
			return;
		}

/*		if (opt_key == "suppress_hyperlinks")
			m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "";
		else*/
			m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
	};

	append_bool_option(m_optgroup_gui, "seq_top_layer_only",
		L("Sequential slider applied only to top layer"),
		L("If enabled, changes made using the sequential slider, in preview, apply only to gcode top layer."
		  "If disabled, changes made using the sequential slider, in preview, apply to the whole gcode."),
		app_config->get("seq_top_layer_only") == "1");

	if (is_editor) {
		append_bool_option(m_optgroup_gui, "show_collapse_button",
			L("Show sidebar collapse/expand button"),
			L("If enabled, the button for the collapse sidebar will be appeared in top right corner of the 3D Scene"),
			app_config->get("show_collapse_button") == "1");
/*
		append_bool_option(m_optgroup_gui, "suppress_hyperlinks",
			L("Suppress to open hyperlink in browser"),
			L("If enabled, PrusaSlicer will not open a hyperlinks in your browser."),
			//L("If enabled, the descriptions of configuration parameters in settings tabs wouldn't work as hyperlinks. "
			//  "If disabled, the descriptions of configuration parameters in settings tabs will work as hyperlinks."),
			app_config->get("suppress_hyperlinks") == "1");
*/
		append_bool_option(m_optgroup_gui, "color_mapinulation_panel",
			L("Use colors for axes values in Manipulation panel"),
			L("If enabled, the axes names and axes values will be colorized according to the axes colors. "
			  "If disabled, old UI will be used."),
			app_config->get("color_mapinulation_panel") == "1");

		append_bool_option(m_optgroup_gui, "order_volumes",
			L("Order object volumes by types"),
			L("If enabled, volumes will be always ordered inside the object. Correct order is Model Part, Negative Volume, Modifier, Support Blocker and Support Enforcer. "
			  "If disabled, you can reorder Model Parts, Negative Volumes and Modifiers. But one of the model parts have to be on the first place."),
			app_config->get("order_volumes") == "1");

		append_bool_option(m_optgroup_gui, "non_manifold_edges",
			L("Show non-manifold edges"),
			L("If enabled, shows non-manifold edges."),
			app_config->get("non_manifold_edges") == "1");

		append_bool_option(m_optgroup_gui, "allow_auto_color_change",
			L("Allow automatically color change"),
			L("If enabled, related notification will be shown, when sliced object looks like a logo or a sign."),
			app_config->get("allow_auto_color_change") == "1");

#ifdef _MSW_DARK_MODE
		append_bool_option(m_optgroup_gui, "tabs_as_menu",
			L("Set settings tabs as menu items (experimental)"),
			L("If enabled, Settings Tabs will be placed as menu items. If disabled, old UI will be used."),
			app_config->get("tabs_as_menu") == "1");
#endif

		m_optgroup_gui->append_separator();

		append_bool_option(m_optgroup_gui, "show_hints",
			L("Show \"Tip of the day\" notification after start"),
			L("If enabled, useful hints are displayed at startup."),
			app_config->get("show_hints") == "1");

		append_enum_option(m_optgroup_gui, "notify_release",
			L("Notify about new releases"),
			L("You will be notified about new release after startup acordingly: All = Regular release and alpha / beta releases. Release only = regular release."),
			new ConfigOptionEnum<NotifyReleaseMode>(static_cast<NotifyReleaseMode>(s_keys_map_NotifyReleaseMode.at(app_config->get("notify_release")))),
			&ConfigOptionEnum<NotifyReleaseMode>::get_enum_values(),
			{"all", "release", "none"},
			{L("All"), L("Release only"), L("None")});

		m_optgroup_gui->append_separator();

		append_bool_option(m_optgroup_gui, "use_custom_toolbar_size",
			L("Use custom size for toolbar icons"),
			L("If enabled, you can change size of toolbar icons manually."),
			app_config->get("use_custom_toolbar_size") == "1");
	}

	activate_options_tab(m_optgroup_gui);

	if (is_editor) {
		// set Field for notify_release to its value to activate the object
		boost::any val = s_keys_map_NotifyReleaseMode.at(app_config->get("notify_release"));
		m_optgroup_gui->get_field("notify_release")->set_value(val, false);

		create_icon_size_slider();
		m_icon_size_sizer->ShowItems(app_config->get("use_custom_toolbar_size") == "1");

		create_settings_mode_widget();
		create_settings_text_color_widget();
		create_settings_mode_color_widget();

		m_optgroup_other = create_options_tab(_L("Other"), tabs);
		m_optgroup_other->m_on_change = [this](t_config_option_key opt_key, boost::any value) {

			if (auto it = m_values.find(opt_key); it != m_values.end() && opt_key != "url_downloader_dest") {
				m_values.erase(it); // we shouldn't change value, if some of those parameters were selected, and then deselected
				return;
			}

			if (opt_key == "suppress_hyperlinks")
				m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "";
			else
				m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
		};


		append_bool_option(m_optgroup_other, "suppress_hyperlinks",
			L("Suppress to open hyperlink in browser"),
			L("If enabled, PrusaSlicer will not open a hyperlinks in your browser."),
			//L("If enabled, the descriptions of configuration parameters in settings tabs wouldn't work as hyperlinks. "
			//  "If disabled, the descriptions of configuration parameters in settings tabs will work as hyperlinks."),
			app_config->get("suppress_hyperlinks") == "1");
		
		append_bool_option(m_optgroup_other, "downloader_url_registered",
			L("Allow downloads from Printables.com"),
			L("If enabled, PrusaSlicer will allow to download from Printables.com"),
			app_config->get("downloader_url_registered") == "1");

		activate_options_tab(m_optgroup_other);

		create_downloader_path_sizer();

#if ENABLE_ENVIRONMENT_MAP
		// Add "Render" tab
		m_optgroup_render = create_options_tab(L("Render"), tabs);
		m_optgroup_render->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			if (auto it = m_values.find(opt_key); it != m_values.end()) {
				m_values.erase(it); // we shouldn't change value, if some of those parameters were selected, and then deselected
				return;
			}
			m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
		};

		append_bool_option(m_optgroup_render, "use_environment_map",
			L("Use environment map"),
			L("If enabled, renders object using the environment map."),
			app_config->get("use_environment_map") == "1");

		activate_options_tab(m_optgroup_render);
#endif // ENABLE_ENVIRONMENT_MAP

#ifdef _WIN32
		// Add "Dark Mode" tab
		m_optgroup_dark_mode = create_options_tab(_L("Dark mode (experimental)"), tabs);
		m_optgroup_dark_mode->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			if (auto it = m_values.find(opt_key); it != m_values.end()) {
				m_values.erase(it); // we shouldn't change value, if some of those parameters were selected, and then deselected
				return;
			}
			m_values[opt_key] = boost::any_cast<bool>(value) ? "1" : "0";
		};

		append_bool_option(m_optgroup_dark_mode, "dark_color_mode",
			L("Enable dark mode"),
			L("If enabled, UI will use Dark mode colors. If disabled, old UI will be used."),
			app_config->get("dark_color_mode") == "1");

		if (wxPlatformInfo::Get().GetOSMajorVersion() >= 10) // Use system menu just for Window newer then Windows 10
															 // Use menu with ownerdrawn items by default on systems older then Windows 10
		{
		append_bool_option(m_optgroup_dark_mode, "sys_menu_enabled",
			L("Use system menu for application"),
			L("If enabled, application will use the standart Windows system menu,\n"
			"but on some combination od display scales it can look ugly. If disabled, old UI will be used."),
			app_config->get("sys_menu_enabled") == "1");
		}

		activate_options_tab(m_optgroup_dark_mode);
#endif //_WIN32
	}

	// update alignment of the controls for all tabs
	update_ctrls_alignment();

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(tabs, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);

	auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	this->Bind(wxEVT_BUTTON, &PreferencesDialog::accept, this, wxID_OK);
	this->Bind(wxEVT_BUTTON, &PreferencesDialog::revert, this, wxID_CANCEL);

	for (int id : {wxID_OK, wxID_CANCEL})
		wxGetApp().UpdateDarkUI(static_cast<wxButton*>(FindWindowById(id, this)));

	sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM | wxTOP, 10);

	SetSizer(sizer);
	sizer->SetSizeHints(this);
	this->CenterOnParent();
}

std::vector<ConfigOptionsGroup*> PreferencesDialog::optgroups()
{
	std::vector<ConfigOptionsGroup*> out;
	out.reserve(4);
	for (ConfigOptionsGroup* opt : { m_optgroup_general.get(), m_optgroup_camera.get(), m_optgroup_gui.get(), m_optgroup_other.get()
#ifdef _WIN32
		, m_optgroup_dark_mode.get()
#endif // _WIN32
#if ENABLE_ENVIRONMENT_MAP
		, m_optgroup_render.get()
#endif // ENABLE_ENVIRONMENT_MAP
	})
		if (opt)
			out.emplace_back(opt);
	return out;
}

void PreferencesDialog::update_ctrls_alignment()
{
	int max_ctrl_width{ 0 };
	for (ConfigOptionsGroup* og : this->optgroups())
		if (int max = og->custom_ctrl->get_max_win_width();
			max_ctrl_width < max)
			max_ctrl_width = max;
	if (max_ctrl_width)
		for (ConfigOptionsGroup* og : this->optgroups())
			og->custom_ctrl->set_max_win_width(max_ctrl_width);
}

void PreferencesDialog::accept(wxEvent&)
{
	if(wxGetApp().is_editor()) {
		if (const auto it = m_values.find("downloader_url_registered"); it != m_values.end())
			downloader->allow(it->second == "1");
		if (!downloader->on_finish())
			return;
#ifdef __linux__
		if( downloader->get_perform_registration_linux()) 
			DesktopIntegrationDialog::perform_desktop_integration(true);
#endif // __linux__
	}

	bool update_filament_sidebar = (m_values.find("no_templates") != m_values.end());

	std::vector<std::string> options_to_recreate_GUI = { "no_defaults", "tabs_as_menu", "sys_menu_enabled" };

	for (const std::string& option : options_to_recreate_GUI) {
		if (m_values.find(option) != m_values.end()) {
			wxString title = wxGetApp().is_editor() ? wxString(SLIC3R_APP_NAME) : wxString(GCODEVIEWER_APP_NAME);
			title += " - " + _L("Changes for the critical options");
			MessageDialog dialog(nullptr,
				_L("Changing some options will trigger application restart.\n"
				   "You will lose the content of the plater.") + "\n\n" +
				_L("Do you want to proceed?"),
				title,
				wxICON_QUESTION | wxYES | wxNO);
			if (dialog.ShowModal() == wxID_YES) {
				m_recreate_GUI = true;
			}
			else {
				for (const std::string& option : options_to_recreate_GUI)
					m_values.erase(option);
			}
			break;
		}
	}

	auto app_config = get_app_config();

	m_seq_top_layer_only_changed = false;
	if (auto it = m_values.find("seq_top_layer_only"); it != m_values.end())
		m_seq_top_layer_only_changed = app_config->get("seq_top_layer_only") != it->second;

	m_settings_layout_changed = false;
	for (const std::string& key : { "old_settings_layout_mode",
								    "new_settings_layout_mode",
								    "dlg_settings_layout_mode" })
	{
	    auto it = m_values.find(key);
	    if (it != m_values.end() && app_config->get(key) != it->second) {
			m_settings_layout_changed = true;
			break;
	    }
	}

#if 0 //#ifdef _WIN32 // #ysDarkMSW - Allow it when we deside to support the sustem colors for application
	if (m_values.find("always_dark_color_mode") != m_values.end())
		wxGetApp().force_sys_colors_update();
#endif

	for (std::map<std::string, std::string>::iterator it = m_values.begin(); it != m_values.end(); ++it)
		app_config->set(it->first, it->second);

	app_config->save();
	if (wxGetApp().is_editor()) {
		wxGetApp().set_label_clr_sys(m_sys_colour->GetColour());
		wxGetApp().set_label_clr_modified(m_mod_colour->GetColour());
		wxGetApp().set_mode_palette(m_mode_palette);
	}

	EndModal(wxID_OK);

#ifdef _WIN32
	if (m_values.find("dark_color_mode") != m_values.end())
		wxGetApp().force_colors_update();
#ifdef _MSW_DARK_MODE
	if (m_values.find("sys_menu_enabled") != m_values.end())
		wxGetApp().force_menu_update();
#endif //_MSW_DARK_MODE
#endif // _WIN32
	
	wxGetApp().update_ui_from_settings();
	clear_cache();

	if (update_filament_sidebar)
		wxGetApp().plater()->sidebar().update_presets(Preset::Type::TYPE_FILAMENT);
}

void PreferencesDialog::revert(wxEvent&)
{
	auto app_config = get_app_config();

	bool save_app_config = false;
	if (m_custom_toolbar_size != atoi(app_config->get("custom_toolbar_size").c_str())) {
		app_config->set("custom_toolbar_size", (boost::format("%d") % m_custom_toolbar_size).str());
		m_icon_size_slider->SetValue(m_custom_toolbar_size);
		save_app_config |= true;
	}
	if (m_use_custom_toolbar_size != (get_app_config()->get("use_custom_toolbar_size") == "1")) {
		app_config->set("use_custom_toolbar_size", m_use_custom_toolbar_size ? "1" : "0");
		save_app_config |= true;

		m_optgroup_gui->set_value("use_custom_toolbar_size", m_use_custom_toolbar_size);
		m_icon_size_sizer->ShowItems(m_use_custom_toolbar_size);
		refresh_og(m_optgroup_gui);
	}
	if (save_app_config)
		app_config->save();


	for (auto value : m_values) {
		const std::string& key = value.first;

		if (key == "default_action_on_dirty_project") {
			m_optgroup_general->set_value(key, app_config->get(key).empty());
			continue;
		}
		if (key == "default_action_on_close_application" || key == "default_action_on_select_preset" || key == "default_action_on_new_project") {
			m_optgroup_general->set_value(key, app_config->get(key) == "none");
			continue;
		}
		if (key == "notify_release") {
			m_optgroup_gui->set_value(key, s_keys_map_NotifyReleaseMode.at(app_config->get(key)));
			continue;
		}
		if (key == "old_settings_layout_mode") {
			m_rb_old_settings_layout_mode->SetValue(app_config->get(key) == "1");
			m_settings_layout_changed = false;
			continue;
		}
		if (key == "new_settings_layout_mode") {
			m_rb_new_settings_layout_mode->SetValue(app_config->get(key) == "1");
			m_settings_layout_changed = false;
			continue;
		}
		if (key == "dlg_settings_layout_mode") {
			m_rb_dlg_settings_layout_mode->SetValue(app_config->get(key) == "1");
			m_settings_layout_changed = false;
			continue;
		}

		for (auto opt_group : { m_optgroup_general, m_optgroup_camera, m_optgroup_gui, m_optgroup_other
#ifdef _WIN32
			, m_optgroup_dark_mode
#endif // _WIN32
#if ENABLE_ENVIRONMENT_MAP
			, m_optgroup_render
#endif // ENABLE_ENVIRONMENT_MAP
			}) {
			if (opt_group->set_value(key, app_config->get(key) == "1"))
				break;
		}
		if (key == "tabs_as_menu") {
			m_rb_new_settings_layout_mode->Show(app_config->get(key) != "1");
			refresh_og(m_optgroup_gui);
			continue;
		}
	}

	clear_cache();
	EndModal(wxID_CANCEL);
}

void PreferencesDialog::msw_rescale()
{
	for (ConfigOptionsGroup* og : this->optgroups())
		og->msw_rescale();

	update_ctrls_alignment();

    msw_buttons_rescale(this, em_unit(), { wxID_OK, wxID_CANCEL });

    layout();
}

void PreferencesDialog::on_sys_color_changed()
{
#ifdef _WIN32
	wxGetApp().UpdateDlgDarkUI(this);
#endif
}

void PreferencesDialog::layout()
{
    const int em = em_unit();

    SetMinSize(wxSize(47 * em, 28 * em));
    Fit();

    Refresh();
}

void PreferencesDialog::clear_cache()
{
	m_values.clear();
	m_custom_toolbar_size = -1;
}

void PreferencesDialog::refresh_og(std::shared_ptr<ConfigOptionsGroup> og)
{
	og->parent()->Layout();
	tabs->Layout();
	this->layout();
}

void PreferencesDialog::create_icon_size_slider()
{
    const auto app_config = get_app_config();

    const int em = em_unit();

    m_icon_size_sizer = new wxBoxSizer(wxHORIZONTAL);

	wxWindow* parent = m_optgroup_gui->parent();
	wxGetApp().UpdateDarkUI(parent);

    if (isOSX)
        // For correct rendering of the slider and value label under OSX
        // we should use system default background
        parent->SetBackgroundStyle(wxBG_STYLE_ERASE);

    auto label = new wxStaticText(parent, wxID_ANY, _L("Icon size in a respect to the default size") + " (%) :");

    m_icon_size_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL| wxRIGHT | (isOSX ? 0 : wxLEFT), em);

    const int def_val = atoi(app_config->get("custom_toolbar_size").c_str());

    long style = wxSL_HORIZONTAL;
    if (!isOSX)
        style |= wxSL_LABELS | wxSL_AUTOTICKS;

    m_icon_size_slider = new wxSlider(parent, wxID_ANY, def_val, 30, 100, 
                               wxDefaultPosition, wxDefaultSize, style);

    m_icon_size_slider->SetTickFreq(10);
    m_icon_size_slider->SetPageSize(10);
    m_icon_size_slider->SetToolTip(_L("Select toolbar icon size in respect to the default one."));

    m_icon_size_sizer->Add(m_icon_size_slider, 1, wxEXPAND);

    wxStaticText* val_label{ nullptr };
    if (isOSX) {
        val_label = new wxStaticText(parent, wxID_ANY, wxString::Format("%d", def_val));
        m_icon_size_sizer->Add(val_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, em);
    }

    m_icon_size_slider->Bind(wxEVT_SLIDER, ([this, val_label, app_config](wxCommandEvent e) {
        auto val = m_icon_size_slider->GetValue();

		app_config->set("custom_toolbar_size", (boost::format("%d") % val).str());
		app_config->save();
		wxGetApp().plater()->get_current_canvas3D()->render();

        if (val_label)
            val_label->SetLabelText(wxString::Format("%d", val));
    }), m_icon_size_slider->GetId());

    for (wxWindow* win : std::vector<wxWindow*>{ m_icon_size_slider, label, val_label }) {
        if (!win) continue;         
        win->SetFont(wxGetApp().normal_font());

        if (isOSX) continue; // under OSX we use wxBG_STYLE_ERASE
        win->SetBackgroundStyle(wxBG_STYLE_PAINT);
    }

	m_optgroup_gui->sizer->Add(m_icon_size_sizer, 0, wxEXPAND | wxALL, em);
}

void PreferencesDialog::create_settings_mode_widget()
{
	wxWindow* parent = m_optgroup_gui->parent();
	wxGetApp().UpdateDarkUI(parent);

	wxString title = L("Layout Options");
    wxStaticBox* stb = new wxStaticBox(parent, wxID_ANY, _(title));
	wxGetApp().UpdateDarkUI(stb);
	if (!wxOSX) stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
	stb->SetFont(wxGetApp().normal_font());

	wxSizer* stb_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

	auto app_config = get_app_config();
	std::vector<wxString> choices = {	_L("Old regular layout with the tab bar"),
										_L("New layout, access via settings button in the top menu"),
										_L("Settings in non-modal window") };
	int id = -1;
	auto add_radio = [this, parent, stb_sizer, choices](wxRadioButton** rb, int id, bool select) {
		*rb = new wxRadioButton(parent, wxID_ANY, choices[id], wxDefaultPosition, wxDefaultSize, id == 0 ? wxRB_GROUP : 0);
		stb_sizer->Add(*rb);
		(*rb)->SetValue(select);
		(*rb)->Bind(wxEVT_RADIOBUTTON, [this, id](wxCommandEvent&) {
			m_values["old_settings_layout_mode"] = (id == 0) ? "1" : "0";
			m_values["new_settings_layout_mode"] = (id == 1) ? "1" : "0";
			m_values["dlg_settings_layout_mode"] = (id == 2) ? "1" : "0";
		});
	};

	add_radio(&m_rb_old_settings_layout_mode, ++id, app_config->get("old_settings_layout_mode") == "1");
	add_radio(&m_rb_new_settings_layout_mode, ++id, app_config->get("new_settings_layout_mode") == "1");
	add_radio(&m_rb_dlg_settings_layout_mode, ++id, app_config->get("dlg_settings_layout_mode") == "1");

#ifdef _MSW_DARK_MODE
	if (app_config->get("tabs_as_menu") == "1") {
		m_rb_new_settings_layout_mode->Hide();
		if (m_rb_new_settings_layout_mode->GetValue()) {
			m_rb_new_settings_layout_mode->SetValue(false);
			m_rb_old_settings_layout_mode->SetValue(true);
		}
	}
#endif

	std::string opt_key = "settings_layout_mode";
	m_blinkers[opt_key] = new BlinkingBitmap(parent);

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_blinkers[opt_key], 0, wxRIGHT, 2);
	sizer->Add(stb_sizer, 1, wxALIGN_CENTER_VERTICAL);
	m_optgroup_gui->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());

	append_preferences_option_to_searcer(m_optgroup_gui, opt_key, title);
}

void PreferencesDialog::create_settings_text_color_widget()
{
	wxWindow* parent = m_optgroup_gui->parent();

	wxString title = L("Text colors");
	wxStaticBox* stb = new wxStaticBox(parent, wxID_ANY, _(title));
	wxGetApp().UpdateDarkUI(stb);
	if (!wxOSX) stb->SetBackgroundStyle(wxBG_STYLE_PAINT);

	std::string opt_key = "text_colors";
	m_blinkers[opt_key] = new BlinkingBitmap(parent);

	wxSizer* stb_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);
	GUI_Descriptions::FillSizerWithTextColorDescriptions(stb_sizer, parent, &m_sys_colour, &m_mod_colour);

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_blinkers[opt_key], 0, wxRIGHT, 2);
	sizer->Add(stb_sizer, 1, wxALIGN_CENTER_VERTICAL);

	m_optgroup_gui->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());

	append_preferences_option_to_searcer(m_optgroup_gui, opt_key, title);
}

void PreferencesDialog::create_settings_mode_color_widget()
{
	wxWindow* parent = m_optgroup_gui->parent();

	wxString title = L("Mode markers");
	wxStaticBox* stb = new wxStaticBox(parent, wxID_ANY, _(title));
	wxGetApp().UpdateDarkUI(stb);
	if (!wxOSX) stb->SetBackgroundStyle(wxBG_STYLE_PAINT);

	std::string opt_key = "mode_markers";
	m_blinkers[opt_key] = new BlinkingBitmap(parent);

	wxSizer* stb_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    // Mode color markers description
	m_mode_palette = wxGetApp().get_mode_palette();
	GUI_Descriptions::FillSizerWithModeColorDescriptions(stb_sizer, parent, { &m_mode_simple, &m_mode_advanced, &m_mode_expert }, m_mode_palette);

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_blinkers[opt_key], 0, wxRIGHT, 2);
	sizer->Add(stb_sizer, 1, wxALIGN_CENTER_VERTICAL);

	m_optgroup_gui->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());

	append_preferences_option_to_searcer(m_optgroup_gui, opt_key, title);
}

void PreferencesDialog::create_downloader_path_sizer()
{
	wxWindow* parent = m_optgroup_other->parent();

	wxString title = L("Download path");
	std::string opt_key = "url_downloader_dest";
	m_blinkers[opt_key] = new BlinkingBitmap(parent);

	downloader = new DownloaderUtils::Worker(parent);

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(m_blinkers[opt_key], 0, wxRIGHT, 2);
	sizer->Add(downloader, 1, wxALIGN_CENTER_VERTICAL);

	m_optgroup_other->sizer->Add(sizer, 0, wxEXPAND | wxTOP, em_unit());

	append_preferences_option_to_searcer(m_optgroup_other, opt_key, title);
}

void PreferencesDialog::init_highlighter(const t_config_option_key& opt_key)
{
	if (m_blinkers.find(opt_key) != m_blinkers.end())
		if (BlinkingBitmap* blinker = m_blinkers.at(opt_key); blinker) {
			m_highlighter.init(blinker);
			return;
		}

	for (auto opt_group : { m_optgroup_general, m_optgroup_camera, m_optgroup_gui, m_optgroup_other
#ifdef _WIN32
		, m_optgroup_dark_mode
#endif // _WIN32
#if ENABLE_ENVIRONMENT_MAP
		, m_optgroup_render
#endif // ENABLE_ENVIRONMENT_MAP
		}) {
		std::pair<OG_CustomCtrl*, bool*> ctrl = opt_group->get_custom_ctrl_with_blinking_ptr(opt_key, -1);
		if (ctrl.first && ctrl.second) {
			m_highlighter.init(ctrl);
			break;
		}
	}
}

} // GUI
} // Slic3r
