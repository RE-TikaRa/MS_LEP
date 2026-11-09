#include "laser_console.h"

static GResource *application_resources;

static void load_application_styles(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".connection-form-row {"
        "  min-height: 58px;"
        "  font-size: 1.10em;"
        "}"
        ".connection-control {"
        "  border-radius: 8px;"
        "  padding: 4px 8px;"
        "}"
        ".connection-control > * {"
        "  min-height: 42px;"
        "}"
        ".connection-primary {"
        "  min-height: 44px;"
        "  padding: 6px 18px;"
        "}"
        ".connection-side-caption {"
        "  font-size: 1.05em;"
        "}"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void register_resources(void)
{
    GError *error = NULL;
    gchar *executable_path = g_file_read_link("/proc/self/exe", &error);
    if (executable_path == NULL) {
        g_error("Unable to locate application resources: %s", error->message);
    }

    gchar *directory = g_path_get_dirname(executable_path);
    gchar *resource_path = g_build_filename(directory, "ms_lep.gresource", NULL);
    application_resources = g_resource_load(resource_path, &error);
    if (application_resources == NULL) {
        g_error("Unable to load application resources: %s", error->message);
    }
    g_resources_register(application_resources);
    g_free(resource_path);
    g_free(directory);
    g_free(executable_path);
}

static void activate(AdwApplication *application, gpointer user_data)
{
    (void)user_data;
    GtkWidget *window = laser_console_create(application);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv)
{
    register_resources();
    adw_init();
    load_application_styles();
    g_set_application_name("MS LEP");
    AdwApplication *application = adw_application_new("io.tikara.mslep", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
