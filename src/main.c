#include "laser_console.h"

static GResource *application_resources;

static void startup(GtkApplication *application, gpointer user_data)
{
    (void)application;
    (void)user_data;
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        ".connection-form-row {"
        "  min-height: 58px;"
        "  font-size: 1.10em;"
        "}"
        ".connection-primary {"
        "  min-height: 44px;"
        "  padding: 6px 18px;"
        "}"
        ".connection-side-caption {"
        "  font-size: 1.05em;"
        "}"
        ".section-title, .title-4 { font-weight: bold; }"
        ".title-1 { font-size: 1.4em; font-weight: bold; }"
        ".success { color: @success_color; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
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

static void activate(GtkApplication *application, gpointer user_data)
{
    (void)user_data;
    GtkWindow *window = gtk_application_get_active_window(application);
    if (window == NULL) {
        window = GTK_WINDOW(laser_console_create(application));
    }
    gtk_window_present(window);
}

int main(int argc, char **argv)
{
    register_resources();
    g_set_application_name("MS LEP");
    GtkApplication *application = gtk_application_new("io.tikara.mslep", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(application, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    g_resources_unregister(application_resources);
    g_resource_unref(application_resources);
    return status;
}
