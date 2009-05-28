#include <gtk/gtk.h>
#include <gdk/gdkscreen.h>
#include <cairo.h>
#include <math.h>

/* This just attempts to paint itself as many times as possible and outputs
   how many times it has managed. You can then run 'xresponse -i' to check 
   how fast hildon-desktop is rendering it. */

/* Area's width and height to render */
#define AREAW 200
#define AREAH 200

static gboolean expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data);
static void clicked(GtkWindow *win, GdkEventButton *event, gpointer user_data);

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Speed test");
    g_signal_connect(G_OBJECT(window), "delete-event", gtk_main_quit, NULL);


    gtk_widget_set_app_paintable(window, TRUE);
    //gtk_widget_set_double_buffered(window, FALSE);

    g_signal_connect(G_OBJECT(window), "expose-event", G_CALLBACK(expose), NULL);

    /* toggle title bar on click - we add the mask to tell X we are interested in this event */
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_widget_add_events(window, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(window), "button-press-event", G_CALLBACK(clicked), NULL);

    /* Run the program */
    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}


/* This is called when we need to draw the windows contents */
static gboolean expose(GtkWidget *widget, GdkEventExpose *event, gpointer userdata)
{
    cairo_t *cr = gdk_cairo_create(widget->window);
    static GTimer *timer = 0;
    static int frame = 0;
    static double last_time = 0;
    double this_time;
    int width, height;
    
    if (!timer) timer = g_timer_new();
    this_time = g_timer_elapsed (timer, NULL);

    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0); /* opaque white */

    /* draw the background */
    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    //cairo_paint (cr);

    
    gtk_window_get_size(GTK_WINDOW(widget), &width, &height);

    cairo_set_source_rgb(cr, 1, (sin(this_time)+1.0)/2.0, 0.2);
    cairo_rectangle(cr, 0, 0, AREAW, AREAH);
    cairo_fill(cr);
    cairo_stroke(cr);

    cairo_destroy(cr);

    if (event->count == 0)
      {
        frame++;
        if (frame > 100) 
          {	    
	    double ms = ((this_time - last_time)*1000) / frame;
	    printf("Redraw time: %f ms per frame - %f fps\n", ms, 1000/ms);
	    last_time = this_time;
	    frame = 0;
	  }
      }

    gtk_widget_queue_draw_area(widget, 0, 0, AREAW, AREAH);

    return FALSE;
}

static void clicked(GtkWindow *win, GdkEventButton *event, gpointer user_data)
{
    /* toggle window manager frames */
    gtk_window_set_decorated(win, !gtk_window_get_decorated(win));
}

