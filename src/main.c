#include "laser_console.h"

static void activate(AdwApplication *application, gpointer user_data)
{
    (void)user_data;
    GtkWidget *window = laser_console_create(application);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv)
{
    adw_init();
    g_set_application_name("MS LEP");
    AdwApplication *application = adw_application_new("io.tikara.mslep", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
