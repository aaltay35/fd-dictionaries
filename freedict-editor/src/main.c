#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>

#include "interface.h"
#include "support.h"

GtkWidget* app1;

int
main (int argc, char *argv[])
{
  // g_thread_supported() should be renamed to g_thread_initialized()
  if(!g_thread_supported())
  {
    g_printerr("Initializing thread system\n");
    g_thread_init (NULL);
  }
  
  gdk_threads_init ();

  poptContext con;
  GnomeProgram *app = gnome_program_init (PACKAGE, VERSION, LIBGNOMEUI_MODULE,
                      argc, argv,
                      GNOME_PARAM_APP_DATADIR, PACKAGE_DATA_DIR,
		      LIBGNOMEUI_PARAM_DEFAULT_ICON,
		        PACKAGE_DATA_DIR "/pixmaps/" PACKAGE "/freedict.png",
                      NULL);

  g_object_get(G_OBJECT(app), GNOME_PARAM_POPT_CONTEXT, &con, NULL);
  extern char *selected_filename;
  selected_filename = (char *) poptGetArg(con);
 
  poptFreeContext(con);
  
  app1 = create_app1();
  gtk_widget_show_all(app1);

  extern void myload(const char *filename);
  if(selected_filename) myload(selected_filename);

//gdk_threads_enter ();

  gtk_main();

//gdk_threads_leave ();

  return 0;
}