#include "laser_console.h"

static GResource *application_resources;

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
    g_set_application_name("MS LEP");
    AdwApplication *application = adw_application_new("io.tikara.mslep", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
