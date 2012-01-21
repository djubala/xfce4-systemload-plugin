/*
 * Copyright (c) 2003 Riccardo Persichetti <riccardo.persichetti@tin.it>
 * Copyright (c) 2010 Florian Rivoal <frivoal@xfce.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include "cpu.h"
#include "memswap.h"
#include "uptime.h"

/* for xml: */
static gchar *MONITOR_ROOT[] = { "SL_Cpu", "SL_Mem", "SL_Swap", "SL_Uptime" };

static gchar *DEFAULT_TEXT[] = { "cpu", "mem", "swap" };
static gchar *DEFAULT_COLOR[] = { "#0000c0", "#00c000", "#f0f000" };

#define UPDATE_TIMEOUT 250

#define BORDER 8

enum { CPU_MONITOR, MEM_MONITOR, SWAP_MONITOR };

typedef struct
{
    gboolean enabled;
    gboolean use_label;
    GdkColor color;
    gchar    *label_text;
} t_monitor_options;

typedef struct
{
    GtkWidget  *box;
    GtkWidget  *label;
    GtkWidget  *status;
    GtkWidget  *ebox;
    GtkWidget  *tooltip_text;

    gulong     history[4];
    gulong     value_read;

    t_monitor_options options;
} t_monitor;

typedef struct
{
    GtkWidget  *box;
    GtkWidget  *label_up;
    GtkWidget  *label_down;
    GtkWidget  *ebox;
    GtkWidget  *tooltip_text;

    gulong     value_read;
    gboolean   enabled;
} t_uptime_monitor;

typedef struct
{
    XfcePanelPlugin   *plugin;
    GtkWidget         *ebox;
    GtkWidget         *box;
    guint             timeout, timeout_id;
    t_monitor         *monitor[3];
    t_uptime_monitor  *uptime;
} t_global_monitor;

static gint
update_monitors(t_global_monitor *global)
{

    gchar caption[128];
    gulong mem, swap, MTotal, MUsed, STotal, SUsed;
    gint count, days, hours, mins;

    global->monitor[0]->history[0] = read_cpuload();
    read_memswap(&mem, &swap, &MTotal, &MUsed, &STotal, &SUsed);
    global->monitor[1]->history[0] = mem;
    global->monitor[2]->history[0] = swap;
    global->uptime->value_read = read_uptime();

    for(count = 0; count < 3; count++)
    {
        if (global->monitor[count]->options.enabled)
        {
            if (global->monitor[count]->history[0] > 100)
                global->monitor[count]->history[0] = 100;

            global->monitor[count]->value_read =
                (global->monitor[count]->history[0] +
                 global->monitor[count]->history[1] +
                 global->monitor[count]->history[2] +
                 global->monitor[count]->history[3]) / 4;

            global->monitor[count]->history[3] =
                global->monitor[count]->history[2];
            global->monitor[count]->history[2] =
                global->monitor[count]->history[1];
            global->monitor[count]->history[1] =
                global->monitor[count]->history[0];

            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(global->monitor[count]->status),
                 global->monitor[count]->value_read / 100.0);
        }
    }
    if (global->monitor[0]->options.enabled)
    {
        g_snprintf(caption, sizeof(caption), _("System Load: %ld%%"),
                   global->monitor[0]->value_read);
        gtk_label_set_text(GTK_LABEL(global->monitor[0]->tooltip_text), caption);
    }

    if (global->monitor[1]->options.enabled)
    {
        g_snprintf(caption, sizeof(caption), _("Memory: %ldMB of %ldMB used"),
                   MUsed >> 10 , MTotal >> 10);
        gtk_label_set_text(GTK_LABEL(global->monitor[1]->tooltip_text), caption);
    }

    if (global->monitor[2]->options.enabled)
    {
        if (STotal)
            g_snprintf(caption, sizeof(caption), _("Swap: %ldMB of %ldMB used"),
                       SUsed >> 10, STotal >> 10);
        else
            g_snprintf(caption, sizeof(caption), _("No swap"));

        gtk_label_set_text(GTK_LABEL(global->monitor[2]->tooltip_text), caption);
    }

    if (global->uptime->enabled)
    {
        days = global->uptime->value_read / 86400;
        hours = (global->uptime->value_read / 3600) % 24;
        mins = (global->uptime->value_read / 60) % 60;
        g_snprintf(caption, sizeof(caption), ngettext("%d day", "%d days", days), days);
        gtk_label_set_text(GTK_LABEL(global->uptime->label_up),
                           caption);
        g_snprintf(caption, sizeof(caption), "%d:%02d", hours, mins);
        gtk_label_set_text(GTK_LABEL(global->uptime->label_down),
                           caption);

        g_snprintf(caption, sizeof(caption),
                   ngettext("Uptime: %d day %d:%02d", "Uptime: %d days %d:%02d", days),
                   days, hours, mins);
        gtk_label_set_text(GTK_LABEL(global->uptime->tooltip_text), caption);
    }
    return TRUE;
}

static gboolean tooltip_cb0(GtkWidget *widget, gint x, gint y, gboolean keyboard, GtkTooltip *tooltip, t_global_monitor *global)
{
        gtk_tooltip_set_custom(tooltip, global->monitor[0]->tooltip_text);
        return TRUE;
}

static gboolean tooltip_cb1(GtkWidget *widget, gint x, gint y, gboolean keyboard, GtkTooltip *tooltip, t_global_monitor *global)
{
        gtk_tooltip_set_custom(tooltip, global->monitor[1]->tooltip_text);
        return TRUE;
}

static gboolean tooltip_cb2(GtkWidget *widget, gint x, gint y, gboolean keyboard, GtkTooltip *tooltip, t_global_monitor *global)
{
        gtk_tooltip_set_custom(tooltip, global->monitor[2]->tooltip_text);
        return TRUE;
}

static gboolean tooltip_cb3(GtkWidget *widget, gint x, gint y, gboolean keyboard, GtkTooltip *tooltip, t_global_monitor *global)
{
        gtk_tooltip_set_custom(tooltip, global->uptime->tooltip_text);
        return TRUE;
}

static void
monitor_set_orientation (XfcePanelPlugin *plugin, GtkOrientation orientation,
                         t_global_monitor *global)
{
    gint count;

    gtk_widget_hide(GTK_WIDGET(global->ebox));

    if (global->box)
        gtk_container_remove(GTK_CONTAINER(global->ebox), 
                             GTK_WIDGET(global->box));
    
    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        global->box = gtk_hbox_new(FALSE, 0);
    }
    else
    {
        global->box = gtk_vbox_new(FALSE, 0);
    }
    gtk_widget_show(global->box);

    for(count = 0; count < 3; count++)
    {
        global->monitor[count]->label =
            gtk_label_new(global->monitor[count]->options.label_text);
        gtk_widget_show(global->monitor[count]->label);

        global->monitor[count]->status = GTK_WIDGET(gtk_progress_bar_new());

        if (orientation == GTK_ORIENTATION_HORIZONTAL)
        {
            global->monitor[count]->box = GTK_WIDGET(gtk_hbox_new(FALSE, 0));
            gtk_progress_bar_set_orientation(GTK_PROGRESS_BAR(global->monitor[count]->status), GTK_PROGRESS_BOTTOM_TO_TOP);
        }
        else
        {
            global->monitor[count]->box = GTK_WIDGET(gtk_vbox_new(FALSE, 0));
            gtk_progress_bar_set_orientation(GTK_PROGRESS_BAR(global->monitor[count]->status), GTK_PROGRESS_LEFT_TO_RIGHT);
        }

        gtk_box_pack_start(GTK_BOX(global->monitor[count]->box),
                           GTK_WIDGET(global->monitor[count]->label),
                           FALSE, FALSE, 0);

        gtk_widget_show(GTK_WIDGET(global->monitor[count]->box));

        global->monitor[count]->ebox = gtk_event_box_new();
        gtk_widget_show(global->monitor[count]->ebox);
        gtk_container_add(GTK_CONTAINER(global->monitor[count]->ebox),
                          GTK_WIDGET(global->monitor[count]->box));

        gtk_widget_modify_bg(GTK_WIDGET(global->monitor[count]->status),
                             GTK_STATE_PRELIGHT,
                             &global->monitor[count]->options.color);
        gtk_widget_modify_bg(GTK_WIDGET(global->monitor[count]->status),
                             GTK_STATE_SELECTED,
                             &global->monitor[count]->options.color);
        gtk_widget_modify_base(GTK_WIDGET(global->monitor[count]->status),
                               GTK_STATE_SELECTED,
                               &global->monitor[count]->options.color);
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(global->monitor[count]->ebox), FALSE);

        gtk_widget_show(GTK_WIDGET(global->monitor[count]->status));

        gtk_box_pack_start(GTK_BOX(global->monitor[count]->box),
                           GTK_WIDGET(global->monitor[count]->status),
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(global->box),
                           GTK_WIDGET(global->monitor[count]->ebox),
                           FALSE, FALSE, 0);
    }

    global->uptime->ebox = gtk_event_box_new();
    gtk_widget_show(global->uptime->ebox);
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(global->uptime->ebox), FALSE);

    gtk_widget_set_has_tooltip(global->monitor[0]->ebox, TRUE);
    gtk_widget_set_has_tooltip(global->monitor[1]->ebox, TRUE);
    gtk_widget_set_has_tooltip(global->monitor[2]->ebox, TRUE);
    gtk_widget_set_has_tooltip(global->uptime->ebox, TRUE);
    g_signal_connect(global->monitor[0]->ebox, "query-tooltip", G_CALLBACK(tooltip_cb0), global);
    g_signal_connect(global->monitor[1]->ebox, "query-tooltip", G_CALLBACK(tooltip_cb1), global);
    g_signal_connect(global->monitor[2]->ebox, "query-tooltip", G_CALLBACK(tooltip_cb2), global);
    g_signal_connect(global->uptime->ebox, "query-tooltip", G_CALLBACK(tooltip_cb3), global);

    global->uptime->box = GTK_WIDGET(gtk_vbox_new(FALSE, 0));
    gtk_widget_show(GTK_WIDGET(global->uptime->box));

    gtk_container_add(GTK_CONTAINER(global->uptime->ebox),
                      GTK_WIDGET(global->uptime->box));

    global->uptime->label_up = gtk_label_new("");
    gtk_widget_show(global->uptime->label_up);
    gtk_box_pack_start(GTK_BOX(global->uptime->box),
                       GTK_WIDGET(global->uptime->label_up),
                       FALSE, FALSE, 0);
    global->uptime->label_down = gtk_label_new("");
    gtk_widget_show(global->uptime->label_down);
    gtk_box_pack_start(GTK_BOX(global->uptime->box),
                       GTK_WIDGET(global->uptime->label_down),
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(global->box),
                       GTK_WIDGET(global->uptime->ebox),
                       FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(global->ebox), GTK_WIDGET(global->box));
    gtk_widget_show(GTK_WIDGET(global->ebox));

    update_monitors (global);
}

static t_global_monitor *
monitor_control_new(XfcePanelPlugin *plugin)
{
    int count;
    t_global_monitor *global;
    
    global = g_new(t_global_monitor, 1);
    global->plugin = plugin;
    global->timeout = UPDATE_TIMEOUT;
    global->timeout_id = 0;
    global->ebox = gtk_event_box_new();
    gtk_container_set_border_width (GTK_CONTAINER (global->ebox), BORDER/2);
    gtk_widget_show(global->ebox);
    global->box = NULL;

    xfce_panel_plugin_add_action_widget (plugin, global->ebox);
    
    for(count = 0; count < 3; count++)
    {
        global->monitor[count] = g_new(t_monitor, 1);
        global->monitor[count]->options.label_text =
            g_strdup(DEFAULT_TEXT[count]);
        gdk_color_parse(DEFAULT_COLOR[count],
                        &global->monitor[count]->options.color);

        global->monitor[count]->options.use_label = TRUE;
        global->monitor[count]->options.enabled = TRUE;

        global->monitor[count]->history[0] = 0;
        global->monitor[count]->history[1] = 0;
        global->monitor[count]->history[2] = 0;
        global->monitor[count]->history[3] = 0;
        global->monitor[count]->tooltip_text = gtk_label_new(NULL);
        g_object_ref(global->monitor[count]->tooltip_text);

    }
    
    global->uptime = g_new(t_uptime_monitor, 1);
    global->uptime->enabled = TRUE;
    global->uptime->tooltip_text = gtk_label_new(NULL);
    g_object_ref(global->uptime->tooltip_text);
    
    return global;
}

static void
monitor_free(XfcePanelPlugin *plugin, t_global_monitor *global)
{
    gint count;

    if (global->timeout_id)
        g_source_remove(global->timeout_id);

    for(count = 0; count < 3; count++)
    {
        if (global->monitor[count]->options.label_text)
            g_free(global->monitor[count]->options.label_text);
        gtk_widget_destroy(global->monitor[count]->tooltip_text);
        g_free(global->monitor[count]);
    }

    gtk_widget_destroy(global->uptime->tooltip_text);


    g_free(global->uptime);

    g_free(global);
}

static void
setup_monitor(t_global_monitor *global)
{
    gint count;

    gtk_widget_hide(GTK_WIDGET(global->uptime->ebox));

    for(count = 0; count < 3; count++)
    {
        gtk_widget_hide(GTK_WIDGET(global->monitor[count]->ebox));
        gtk_widget_hide(global->monitor[count]->label);
        gtk_label_set_text(GTK_LABEL(global->monitor[count]->label),
                           global->monitor[count]->options.label_text);

        gtk_widget_modify_bg(GTK_WIDGET(global->monitor[count]->status),
                             GTK_STATE_PRELIGHT,
                             &global->monitor[count]->options.color);
        gtk_widget_modify_bg(GTK_WIDGET(global->monitor[count]->status),
                             GTK_STATE_SELECTED,
                             &global->monitor[count]->options.color);
        gtk_widget_modify_base(GTK_WIDGET(global->monitor[count]->status),
                               GTK_STATE_SELECTED,
                               &global->monitor[count]->options.color);

        if(global->monitor[count]->options.enabled)
        {
            gtk_widget_show(GTK_WIDGET(global->monitor[count]->ebox));
            if (global->monitor[count]->options.use_label)
                gtk_widget_show(global->monitor[count]->label);

            gtk_widget_show(GTK_WIDGET(global->monitor[count]->status));
        }
    }
    if(global->uptime->enabled)
    {
        if (global->monitor[0]->options.enabled ||
            global->monitor[1]->options.enabled ||
            global->monitor[2]->options.enabled)
        {
            gtk_container_set_border_width(GTK_CONTAINER(global->uptime->box), 2);
        }
        gtk_widget_show(GTK_WIDGET(global->uptime->ebox));
    }
}

static void
monitor_read_config(XfcePanelPlugin *plugin, t_global_monitor *global)
{
    gint count;
    const char *value;
    char *file;
    XfceRc *rc;
    
    if (!(file = xfce_panel_plugin_lookup_rc_file (plugin)))
        return;
    
    rc = xfce_rc_simple_open (file, TRUE);
    g_free (file);

    if (!rc)
        return;
    
    if (xfce_rc_has_group (rc, "Main"))
    {
        xfce_rc_set_group (rc, "Main");
        global->timeout = xfce_rc_read_int_entry (rc, "Timeout", global->timeout);
    }

    for(count = 0; count < 3; count++)
    {
        if (xfce_rc_has_group (rc, MONITOR_ROOT[count]))
        {
            xfce_rc_set_group (rc, MONITOR_ROOT[count]);
            
            global->monitor[count]->options.enabled = 
                xfce_rc_read_bool_entry (rc, "Enabled", TRUE);

            global->monitor[count]->options.use_label = 
                xfce_rc_read_bool_entry (rc, "Use_Label", TRUE);
            
            if ((value = xfce_rc_read_entry (rc, "Color", NULL)))
            {
                gdk_color_parse(value,
                                &global->monitor[count]->options.color);
            }
            if ((value = xfce_rc_read_entry (rc, "Text", NULL)) && *value)
            {
                if (global->monitor[count]->options.label_text)
                    g_free(global->monitor[count]->options.label_text);
                global->monitor[count]->options.label_text =
                    g_strdup(value);
            }
        }
        if (xfce_rc_has_group (rc, MONITOR_ROOT[3]))
        {
            xfce_rc_set_group (rc, MONITOR_ROOT[3]);
            
            global->uptime->enabled = 
                xfce_rc_read_bool_entry (rc, "Enabled", TRUE);
        }
    }

    xfce_rc_close (rc);
}

static void
monitor_write_config(XfcePanelPlugin *plugin, t_global_monitor *global)
{
    char value[10];
    gint count;
    XfceRc *rc;
    char *file;

    if (!(file = xfce_panel_plugin_save_location (plugin, TRUE)))
        return;
    
    rc = xfce_rc_simple_open (file, FALSE);
    g_free (file);

    if (!rc)
        return;

    xfce_rc_set_group (rc, "Main");
    xfce_rc_write_int_entry (rc, "Timeout", global->timeout);

    for(count = 0; count < 3; count++)
    {
        xfce_rc_set_group (rc, MONITOR_ROOT[count]);

        xfce_rc_write_bool_entry (rc, "Enabled", 
                global->monitor[count]->options.enabled);
        
        xfce_rc_write_bool_entry (rc, "Use_Label", 
                global->monitor[count]->options.use_label);

        g_snprintf(value, 8, "#%02X%02X%02X",
                   (guint)global->monitor[count]->options.color.red >> 8,
                   (guint)global->monitor[count]->options.color.green >> 8,
                   (guint)global->monitor[count]->options.color.blue >> 8);

        xfce_rc_write_entry (rc, "Color", value);

        xfce_rc_write_entry (rc, "Text", 
            global->monitor[count]->options.label_text ?
                global->monitor[count]->options.label_text : "");
    }

    xfce_rc_set_group (rc, MONITOR_ROOT[3]);

    xfce_rc_write_bool_entry (rc, "Enabled",
            global->uptime->enabled);

    xfce_rc_close (rc);
}

static gboolean
monitor_set_size(XfcePanelPlugin *plugin, int size, t_global_monitor *global)
{
    gint count;

    for(count = 0; count < 3; count++)
    {
        if (xfce_panel_plugin_get_orientation (plugin) == 
                GTK_ORIENTATION_HORIZONTAL)
        {
            gtk_widget_set_size_request(GTK_WIDGET(global->monitor[count]->status),
                                        BORDER, size - BORDER);
        }
        else
        {
            gtk_widget_set_size_request(GTK_WIDGET(global->monitor[count]->status),
                                        size - BORDER, BORDER);
        }
    }
    
    setup_monitor(global);

    return TRUE;
}

static void
entry_changed_cb(GtkEntry *entry, t_global_monitor *global)
{
    gchar** charvar = (gchar**)g_object_get_data (G_OBJECT(entry), "charvar");
    g_free(*charvar);
    *charvar = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    setup_monitor(global);
}

static void
check_button_cb(GtkToggleButton *check_button, t_global_monitor *global)
{
    gboolean* boolvar;
    gpointer sensitive_widget;
    boolvar = (gboolean*)g_object_get_data(G_OBJECT(check_button), "boolvar");
    sensitive_widget = g_object_get_data(G_OBJECT(check_button), "sensitive_widget");
    *boolvar = gtk_toggle_button_get_active(check_button);
    if (sensitive_widget)
        gtk_widget_set_sensitive(GTK_WIDGET(sensitive_widget), *boolvar);
    setup_monitor(global);
}

static void
color_set_cb(GtkColorButton *color_button, t_global_monitor *global)
{
    GdkColor* colorvar;
    colorvar = (GdkColor*)g_object_get_data(G_OBJECT(color_button), "colorvar");
    gtk_color_button_get_color(color_button, colorvar);
    setup_monitor(global);
}

static void
monitor_dialog_response (GtkWidget *dlg, int response, 
                         t_global_monitor *global)
{
    gtk_widget_destroy (dlg);
    xfce_panel_plugin_unblock_menu (global->plugin);
    monitor_write_config (global->plugin, global);
}

static void
change_timeout_cb(GtkSpinButton *spin, t_global_monitor *global)
{
    global->timeout = gtk_spin_button_get_value(spin);

    if (global->timeout_id)
        g_source_remove(global->timeout_id);
    global->timeout_id = g_timeout_add(global->timeout, (GSourceFunc)update_monitors, global);
}

static void
monitor_create_options(XfcePanelPlugin *plugin, t_global_monitor *global)
{
    GtkWidget           *dlg, *content, *frame, *table, *label, *widget;
    guint                count;
    t_monitor           *monitor;
    static const gchar *FRAME_TEXT[] = {
            N_ ("CPU monitor"),
            N_ ("Memory monitor"),
            N_ ("Swap monitor"),
            N_ ("Uptime monitor")
    };

    xfce_panel_plugin_block_menu (plugin);
    
    dlg = xfce_titled_dialog_new_with_buttons (_("System Load Monitor"), 
                     GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (plugin))),
                                               GTK_DIALOG_DESTROY_WITH_PARENT |
                                               GTK_DIALOG_NO_SEPARATOR,
                                               GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
                                               NULL);
    
    g_signal_connect (G_OBJECT (dlg), "response",
                      G_CALLBACK (monitor_dialog_response), global);

    gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name (GTK_WINDOW (dlg), "xfce4-settings");

    content = gtk_dialog_get_content_area (GTK_DIALOG (dlg));

#define ADD(widget, row, column) \
    gtk_table_attach_defaults (GTK_TABLE (table), widget, \
                               column, column+1, row, row+1)
#define ENTRY(row, checktext, boolvar, charvar) \
    widget = gtk_entry_new (); \
    g_object_set_data (G_OBJECT(widget), "charvar", &charvar); \
    gtk_entry_set_text (GTK_ENTRY (widget), charvar); \
    g_signal_connect (G_OBJECT (widget), "changed", \
                      G_CALLBACK (entry_changed_cb), global); \
    label = gtk_check_button_new_with_mnemonic (checktext); \
    g_object_set_data (G_OBJECT(label), "sensitive_widget", widget); \
    g_object_set_data (G_OBJECT(label), "boolvar", &boolvar); \
    g_signal_connect (GTK_WIDGET(label), "toggled", \
                      G_CALLBACK(check_button_cb), global); \
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(label), boolvar); \
    ADD(label, row, 0); ADD(widget, row, 1)
#define COLOR_BUTTON(row, labeltext, colorvar) \
    label = gtk_label_new_with_mnemonic(labeltext); \
    widget = gtk_color_button_new_with_color(&colorvar); \
    g_object_set_data (G_OBJECT(widget), "colorvar", &colorvar); \
    g_signal_connect (G_OBJECT (widget), "color-set", \
                      G_CALLBACK (color_set_cb), global); \
    gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5f); \
    gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget); \
    ADD(label, row, 0); ADD(widget, row, 1)
#define SPIN(row, labeltext, value, min, max, step, callback) \
    label = gtk_label_new_with_mnemonic (labeltext); \
    widget = gtk_spin_button_new_with_range (min, max, step); \
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), value); \
    g_signal_connect (G_OBJECT (widget), "value-changed", \
                      G_CALLBACK (callback), global); \
    gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5f); \
    gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget); \
    ADD(label, row, 0); ADD(widget, row, 1)
#define START_FRAME(title, rows) \
    table = gtk_table_new (rows, 2, FALSE); \
    gtk_table_set_col_spacings (GTK_TABLE (table), 12); \
    gtk_table_set_row_spacings (GTK_TABLE (table), 6); \
    frame = xfce_gtk_frame_box_new_with_content (title, table); \
    gtk_box_pack_start_defaults (GTK_BOX (content), frame)
#define START_FRAME_CHECK(title, rows, boolvar) \
    START_FRAME(title, rows); \
    widget = gtk_check_button_new(); \
    label = gtk_frame_get_label_widget (GTK_FRAME(frame)); \
    g_object_ref(G_OBJECT(label)); \
    gtk_container_remove(GTK_CONTAINER(frame), label); \
    gtk_container_add(GTK_CONTAINER(widget), label); \
    g_object_unref(G_OBJECT(label)); \
    gtk_frame_set_label_widget (GTK_FRAME(frame), widget); \
    g_object_set_data (G_OBJECT(widget), "sensitive_widget", table); \
    g_object_set_data (G_OBJECT(widget), "boolvar", &boolvar); \
    g_signal_connect (GTK_WIDGET(widget), "toggled", \
                      G_CALLBACK(check_button_cb), global); \
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(widget), boolvar)

    START_FRAME(_("General"), 1);
    SPIN(0, _("Update interval (ms):"),
         global->timeout, 100, 10000, 50, change_timeout_cb);
    
    for(count = 0; count < 3; count++)
    {
        monitor = global->monitor[count];

        START_FRAME_CHECK(FRAME_TEXT[count], 2, monitor->options.enabled);
        ENTRY(0, _("Text to display:"),
              monitor->options.use_label, monitor->options.label_text);
        COLOR_BUTTON(1, _("Bar color:"), monitor->options.color);
    }

    /*uptime monitor options - start*/
    START_FRAME_CHECK(FRAME_TEXT[3], 1, global->uptime->enabled);
    /*uptime monitor options - end*/

    gtk_widget_show_all (dlg);
}

static void
systemload_construct (XfcePanelPlugin *plugin)
{
    t_global_monitor *global;
 
    xfce_textdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");
    
    global = monitor_control_new (plugin);

    monitor_read_config (plugin, global);
    
    monitor_set_orientation (plugin, 
                             xfce_panel_plugin_get_orientation (plugin),
                             global);

    setup_monitor (global);

    gtk_container_add (GTK_CONTAINER (plugin), global->ebox);

    update_monitors (global);

    global->timeout_id = 
        g_timeout_add(global->timeout, (GSourceFunc)update_monitors, global);
    
    g_signal_connect (plugin, "free-data", G_CALLBACK (monitor_free), global);

    g_signal_connect (plugin, "save", G_CALLBACK (monitor_write_config), 
                      global);

    g_signal_connect (plugin, "size-changed", G_CALLBACK (monitor_set_size),
                      global);

    g_signal_connect (plugin, "orientation-changed", 
                      G_CALLBACK (monitor_set_orientation), global);

    xfce_panel_plugin_menu_show_configure (plugin);
    g_signal_connect (plugin, "configure-plugin", 
                      G_CALLBACK (monitor_create_options), global);
}

XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL (systemload_construct);

