/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
  Gpredict: Real-time satellite tracking and orbit prediction program

  Copyright (C)  2001-2008  Alexandru Csete, OZ9AEC.

  Authors: Alexandru Csete <oz9aec@gmail.com>

  Comments, questions and bugreports should be submitted via
  http://sourceforge.net/projects/gpredict/
  More details can be found at the project home page:

  http://gpredict.oz9aec.net/

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, visit http://www.fsf.org/
*/
/** \brief Satellite selector.
 *
 */
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#ifdef HAVE_CONFIG_H
#  include <build-config.h>
#endif
#include "sgpsdp/sgp4sdp4.h"
#include "sat-log.h"
#include "gtk-sat-data.h"
#include "compat.h"
#include "gtk-sat-selector.h"







static void gtk_sat_selector_class_init (GtkSatSelectorClass *class);
static void gtk_sat_selector_init       (GtkSatSelector *selector);
static void gtk_sat_selector_destroy    (GtkObject *object);

static void create_and_fill_models      (GtkSatSelector *selector);
static void load_cat_file               (GtkSatSelector *selector, const gchar *fname);
static void group_selected_cb           (GtkComboBox *combobox, gpointer data);
static void row_activated_cb            (GtkTreeView *view,
                                         GtkTreePath *path,
                                         GtkTreeViewColumn *column,
                                         gpointer data);

static gint compare_func (GtkTreeModel *model,
                          GtkTreeIter  *a,
                          GtkTreeIter  *b,
                          gpointer      userdata);

static GtkVBoxClass *parent_class = NULL;


/** \brief GtkSatSelector signal IDs */
enum {
    SAT_ACTIVATED_SIGNAL, /*!< "sat-activated" signal */
    LAST_SIGNAL
};

/** \brief GtkSatSelector specific signals. */
static guint gtksatsel_signals[LAST_SIGNAL] = { 0 };


GType gtk_sat_selector_get_type ()
{
    static GType gtk_sat_selector_type = 0;

    if (!gtk_sat_selector_type)
    {
        static const GTypeInfo gtk_sat_selector_info =
        {
            sizeof (GtkSatSelectorClass),
            NULL,  /* base_init */
            NULL,  /* base_finalize */
            (GClassInitFunc) gtk_sat_selector_class_init,
            NULL,  /* class_finalize */
            NULL,  /* class_data */
            sizeof (GtkSatSelector),
            1,     /* n_preallocs */
            (GInstanceInitFunc) gtk_sat_selector_init,
        };

        gtk_sat_selector_type = g_type_register_static (GTK_TYPE_VBOX,
                                                        "GtkSatSelector",
                                                        &gtk_sat_selector_info,
                                                        0);
    }

    return gtk_sat_selector_type;
}


static void gtk_sat_selector_class_init (GtkSatSelectorClass *class)
{
    GObjectClass      *gobject_class;
    GtkObjectClass    *object_class;
    GtkWidgetClass    *widget_class;
    GtkContainerClass *container_class;

    gobject_class   = G_OBJECT_CLASS (class);
    object_class    = (GtkObjectClass*) class;
    widget_class    = (GtkWidgetClass*) class;
    container_class = (GtkContainerClass*) class;

    parent_class = g_type_class_peek_parent (class);

    object_class->destroy = gtk_sat_selector_destroy;

    /* create GtkSatSelector specific signals */
    gtksatsel_signals[SAT_ACTIVATED_SIGNAL] =
            g_signal_new ("sat-activated",
                          G_TYPE_FROM_CLASS (class),
                          G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                          G_STRUCT_OFFSET (GtkSatSelectorClass,gtksatselector),
                          NULL,
                          NULL,
                          g_cclosure_marshal_VOID__INT,
                          G_TYPE_NONE, // return type
                          1,
                          G_TYPE_INT); // catnum
}


/** \brief Initialise satellite selector widget */
static void gtk_sat_selector_init (GtkSatSelector *selector)
{
    selector->models = NULL;
}



/** \brief Clean up memory before destroying satellite selector widget */
static void gtk_sat_selector_destroy (GtkObject *object)
{
    GtkSatSelector *selector = GTK_SAT_SELECTOR (object);

    /* clear list of selected satellites */
    /* crashes on 2. instance: g_slist_free (sat_tree->selection); */
    guint n,i;
    gpointer data;

    n = g_slist_length (selector->models);

    for (i = 0; i < n; i++) {
        /* get the first element and delete it */
        data = g_slist_nth_data (selector->models, 0);
        selector->models = g_slist_remove (selector->models, data);
    }


    (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/** \brief Create a new GtkSatSelector widget
 *  \param flags Flags indicating which columns should be visible
 *               (see gtk_sat_selector_flag_t)
 *  \return A GtkSatSelector widget.
 */
GtkWidget *gtk_sat_selector_new (guint flags)
{
    GtkWidget          *widget;
    GtkSatSelector     *selector;
    GtkTreeModel       *model;
    GtkCellRenderer    *renderer;
    GtkTreeViewColumn  *column;
    GtkWidget          *table;


    if (!flags)
        flags = GTK_SAT_SELECTOR_DEFAULT_FLAGS;

    widget = g_object_new (GTK_TYPE_SAT_SELECTOR, NULL);
    selector = GTK_SAT_SELECTOR (widget);

    selector->flags = flags;

    /* create group selector combo box (needed by create_and_fill_models()) */
    GTK_SAT_SELECTOR (widget)->groups = gtk_combo_box_new_text ();
    gtk_widget_set_tooltip_text (GTK_SAT_SELECTOR (widget)->groups,
                                 _("Select a satellite group or category to narrow your search."));

    /* combo box signal handler will be connected at the end after it has
       been populated to avoid false triggering */


    /* create list and model */
    selector->tree = gtk_tree_view_new ();
    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (selector->tree), TRUE);
    create_and_fill_models (selector);
    model = GTK_TREE_MODEL (g_slist_nth_data (selector->models, 0));
    gtk_tree_view_set_model (GTK_TREE_VIEW (selector->tree), model);
    g_object_unref (model);

    /* sort the tree by name */
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model),
                                     GTK_SAT_SELECTOR_COL_NAME,
                                     compare_func,
                                     NULL,
                                     NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
                                          GTK_SAT_SELECTOR_COL_NAME,
                                          GTK_SORT_ASCENDING);

    /* we can now connect combobox signal handler */
    g_signal_connect (GTK_SAT_SELECTOR (widget)->groups, "changed",
                      G_CALLBACK(group_selected_cb), widget);


    /* create tree view columns */
    /* label column */
    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes (_("Available Satellites"), renderer,
                                                       "text", GTK_SAT_SELECTOR_COL_NAME,
                                                       NULL);
    gtk_tree_view_insert_column (GTK_TREE_VIEW (selector->tree), column, -1);
    if (!(flags & GTK_SAT_SELECTOR_FLAG_NAME))
        gtk_tree_view_column_set_visible (column, FALSE);

    /* catalogue number */
    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes (_("Catnum"), renderer,
                                                       "text", GTK_SAT_SELECTOR_COL_CATNUM,
                                                       NULL);
    gtk_tree_view_insert_column (GTK_TREE_VIEW (selector->tree), column, -1);
    if (!(flags & GTK_SAT_SELECTOR_FLAG_CATNUM))
        gtk_tree_view_column_set_visible (column, FALSE);

    /* epoch */
    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes (_("Epoch"), renderer,
                                                       "text", GTK_SAT_SELECTOR_COL_EPOCH,
                                                       NULL);
    gtk_tree_view_insert_column (GTK_TREE_VIEW (selector->tree), column, -1);
    if (!(flags & GTK_SAT_SELECTOR_FLAG_EPOCH))
        gtk_tree_view_column_set_visible (column, FALSE);

    /* "row-activated" signal is used to catch double click events, which means
       a satellite has been selected. This propagates to the TBD GtkSatSelector signal */
    g_signal_connect (selector->tree, "row-activated",
                      G_CALLBACK(row_activated_cb), selector);

    /* scrolled window */
    GTK_SAT_SELECTOR (widget)->swin = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (GTK_SAT_SELECTOR (widget)->swin),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);


    gtk_container_add (GTK_CONTAINER (GTK_SAT_SELECTOR (widget)->swin),
                       GTK_SAT_SELECTOR (widget)->tree);


    //gtk_container_add (GTK_CONTAINER (widget), GTK_SAT_TREE (widget)->swin);
    //gtk_box_pack_start (GTK_BOX (widget), GTK_SAT_SELECTOR (widget)->groups, FALSE, FALSE, 0);
    //gtk_box_pack_start (GTK_BOX (widget), GTK_SAT_SELECTOR (widget)->swin, TRUE, TRUE, 0);

    table = gtk_table_new (7, 4, TRUE);

    /* Search */    
    gtk_table_attach (GTK_TABLE (table), gtk_label_new (_("Search:")), 0, 1, 0, 1,
                      GTK_SHRINK, GTK_SHRINK, 0, 0);
    GTK_SAT_SELECTOR (widget)->search = gtk_entry_new ();

    /* this enables automatic search */
    gtk_tree_view_set_search_entry (GTK_TREE_VIEW (GTK_SAT_SELECTOR (widget)->tree),
                                    GTK_ENTRY (GTK_SAT_SELECTOR (widget)->search));
    gtk_table_attach (GTK_TABLE (table), GTK_SAT_SELECTOR (widget)->search, 1, 4, 0, 1,
                      GTK_FILL, GTK_FILL, 0, 0);

    /* Group selector */
    gtk_table_attach (GTK_TABLE (table), gtk_label_new (_("Group:")), 0, 1, 1, 2,
                      GTK_SHRINK, GTK_SHRINK, 0, 0);
    gtk_table_attach (GTK_TABLE (table), GTK_SAT_SELECTOR (widget)->groups, 1, 4, 1, 2,
                      GTK_FILL, GTK_FILL, 0, 0);


    /* satellite list */
    gtk_table_attach (GTK_TABLE (table), GTK_SAT_SELECTOR (widget)->swin, 0, 4, 2, 7,
                      GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

    /* Add tabel to main container */
    gtk_box_pack_start (GTK_BOX (widget), table, TRUE, TRUE, 0);

    gtk_widget_show_all (widget);


    /* initialise selection */
    //GTK_SAT_TREE (widget)->selection = NULL;


    return widget;
}



/** \brief Create and fill data store models.
  * \param selector Pointer to the GtkSatSelector widget
  *
  * this fuinction scan for satellite data and stores them in tree models
  * that can be displayed in a tree view. The scan is performed in two iterations:
  *
  * (1) First, all .sat files are scanned, read and added to a pseudo-group called
  *     "all" satellites.
  * (2) After the first scane, the function scans and reads .cat files and creates
  *     the groups accordingly.
  *
  * For each group (including the "all" group) and entry is added to the
  * selector->groups GtkComboBox, where the index of the entry corresponds to
  * the index of the group model in selector->models.
  */
static void create_and_fill_models (GtkSatSelector *selector)
{
    GtkListStore *store;    /* the list store data structure */
    GtkTreeIter   node;     /* new top level node added to the tree store */
    GDir         *dir;
    gchar        *dirname;
    sat_t         sat;
    gint          catnum;

    gchar       **buffv;
    const gchar  *fname;

    guint         num = 0;




    /* load all satellites into selector->models[0] */
    store = gtk_list_store_new (GTK_SAT_SELECTOR_COL_NUM,
                                G_TYPE_STRING,    // name
                                G_TYPE_INT,       // catnum
                                G_TYPE_DOUBLE     // epoch
                                );
    selector->models = g_slist_append (selector->models, store);
    gtk_combo_box_append_text (GTK_COMBO_BOX (selector->groups), _("All satellites"));
    gtk_combo_box_set_active (GTK_COMBO_BOX (selector->groups), 0);

    dirname = get_satdata_dir ();
    dir = g_dir_open (dirname, 0, NULL);
    if (!dir) {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%s: Failed to open satdata directory %s."),
                     __FILE__, __FUNCTION__, dirname);

        g_free (dirname);

        return;
    }

    /* Scan data directory for .sat files.
       For each file scan through the file and
       add entry to the tree.
        */
    while ((fname = g_dir_read_name (dir))) {

        if (g_strrstr (fname, ".sat")) {

            buffv = g_strsplit (fname, ".", 0);
            catnum = (gint) g_ascii_strtoll (buffv[0], NULL, 0);

            if (gtk_sat_data_read_sat (catnum, &sat)) {
                /* error */
            }
            else {
                /* read satellite */

                gtk_list_store_append (store, &node);
                gtk_list_store_set (store, &node,
                                    GTK_SAT_SELECTOR_COL_NAME, sat.nickname,
                                    GTK_SAT_SELECTOR_COL_CATNUM, catnum,
                                    GTK_SAT_SELECTOR_COL_EPOCH, sat.jul_epoch,
                                    -1);

                g_free (sat.name);
                g_free (sat.nickname);
                num++;
            }

            g_strfreev (buffv);
        }
    }
    sat_log_log (SAT_LOG_LEVEL_MSG,
                 _("%s:%s: Read %d satellites into MAIN group."),
                 __FILE__, __FUNCTION__, num);

    /* load satellites from each .cat file into selector->models[i] */
    g_dir_rewind (dir);
    while ((fname = g_dir_read_name (dir))) {
        if (g_strrstr (fname, ".cat")) {

            load_cat_file (selector, fname);

        }
    }

    g_dir_close (dir);
    g_free (dirname);

}



/** \brief Load satellites from a .cat file
  * \param selector Pointer to the GtkSatSelector
  * \param fname The name of the .cat file (name only, no path)
  *
  * This function is used to encapsulate reading the clear text name and the contents
  * of a .cat file. It is used for building the satellite tree store models
  */
static void load_cat_file (GtkSatSelector *selector, const gchar *fname)
{
    GIOChannel   *catfile;
    GError       *error = NULL;
    GtkListStore *store;    /* the list store data structure */
    GtkTreeIter   node;     /* new top level node added to the tree store */

    gchar        *path;
    gchar        *buff;
    sat_t         sat;
    gint          catnum;
    guint         num = 0;


    /* .cat files contains clear text category name in the first line
               then one satellite catalog number per line */
    path = sat_file_name (fname);
    catfile = g_io_channel_new_file (path, "r", &error);
    if (error != NULL) {
        sat_log_log (SAT_LOG_LEVEL_ERROR,
                     _("%s:%s: Failed to open %s: %s"),
                     __FILE__, __FUNCTION__, fname, error->message);
        g_clear_error (&error);
    }
    else {
        /* read first line => category name */

        if (g_io_channel_read_line (catfile, &buff, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
            g_strstrip (buff); /* removes trailing newline */
            gtk_combo_box_append_text (GTK_COMBO_BOX (selector->groups), buff);
            g_free (buff);

            /* we can safely create the liststore for this category */
            store = gtk_list_store_new (GTK_SAT_SELECTOR_COL_NUM,
                                        G_TYPE_STRING,    // name
                                        G_TYPE_INT,       // catnum
                                        G_TYPE_DOUBLE     // epoch
                                        );
            selector->models = g_slist_append (selector->models, store);



            /* Remaining lines are catalog numbers for satellites.
               Read line by line until the first error, which hopefully is G_IO_STATUS_EOF
            */
            while (g_io_channel_read_line (catfile, &buff, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {

                /* stip trailing EOL */
                g_strstrip (buff);

                /* catalog number to integer */
                catnum = (gint) g_ascii_strtoll (buff, NULL, 0);

                /* try to read satellite data */
                if (gtk_sat_data_read_sat (catnum, &sat)) {
                    /* error */
                    sat_log_log (SAT_LOG_LEVEL_ERROR,
                                 _("%s:%s: Error reading satellite %d."),
                                 __FILE__, __FUNCTION__, catnum);
                }
                else {
                    /* insert satellite into liststore */
                    gtk_list_store_append (store, &node);
                    gtk_list_store_set (store, &node,
                                        GTK_SAT_SELECTOR_COL_NAME, sat.nickname,
                                        GTK_SAT_SELECTOR_COL_CATNUM, catnum,
                                        GTK_SAT_SELECTOR_COL_EPOCH, sat.jul_epoch,
                                        -1);
                    g_free (sat.name);
                    g_free (sat.nickname);
                    num++;
                }

                g_free (buff);
            }
            sat_log_log (SAT_LOG_LEVEL_MSG,
                         _("%s:%s: Read %d satellites from %s"),
                         __FILE__, __FUNCTION__, num, fname);
        }
        else {
            sat_log_log (SAT_LOG_LEVEL_ERROR,
                         _("%s:%s: Failed to read %s"),
                         __FILE__, __FUNCTION__, fname);
        }

    }

    g_free (path);
    g_io_channel_shutdown (catfile, TRUE, NULL);
}




/** \brief Compare two rows of the GtkSatSelector.
 *  \param model The tree model of the GtkSatSelector.
 *  \param a The first row.
 *  \param b The second row.
 *  \param userdata Not used.
 *
 * This function is used by the sorting algorithm to compare two rows of the
 * GtkSatSelector widget. The unctions works by comparing the character strings
 * in the name column.
 */
static gint compare_func (GtkTreeModel *model,
                          GtkTreeIter  *a,
                          GtkTreeIter  *b,
                          gpointer      userdata)
{
    gchar *sat1,*sat2;
    gint ret = 0;


    gtk_tree_model_get(model, a, GTK_SAT_SELECTOR_COL_NAME, &sat1, -1);
    gtk_tree_model_get(model, b, GTK_SAT_SELECTOR_COL_NAME, &sat2, -1);

    ret = g_ascii_strcasecmp (sat1, sat2);

    g_free (sat1);
    g_free (sat2);

    return ret;
}


/** \brief Signal handler for managing satellite group selections.
  * \param combobox The GtkcomboBox widget.
  * \param data Pointer to the GtkSatSelector widget.
  *
  * This function is called when the user selects a new satellite group in the
  * filter. The function is responsible for reloading the conctents of the satellite
  * list according to the new selection. This task is very simple because the
  * proper liststore has already been constructed and stored in selector->models[i]
  * where i corresponds to the index of the newly selected group in the combo box.
  */
static void group_selected_cb (GtkComboBox *combobox, gpointer data)
{
    GtkSatSelector *selector = GTK_SAT_SELECTOR (data);
    GtkTreeModel   *newmodel;
    GtkTreeModel   *oldmodel;
    gint            sel;

    sel = gtk_combo_box_get_active (combobox);


    /* Frst, we need to reference the existing model, otherwise its
       refcount will drop to 0 when we replace it */
    oldmodel = gtk_tree_view_get_model (GTK_TREE_VIEW (selector->tree));
    if (oldmodel != NULL)
        g_object_ref (oldmodel);

    /* now replace oldmodel with newmodel */
    newmodel = GTK_TREE_MODEL (g_slist_nth_data (selector->models, sel));
    gtk_tree_view_set_model (GTK_TREE_VIEW (selector->tree), newmodel);
    g_object_unref (newmodel);

    /* We changed the GtkTreeModel so we need to reset the sort column ID */
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (newmodel),
                                          GTK_SAT_SELECTOR_COL_NAME,
                                          GTK_SORT_ASCENDING);


}


/** \brief Signal handler for managing satellite selection.
  * \param view Pointer to the GtkTreeView object.
  * \param path The path of the row that was activated.
  * \param column The column where the activation occured.
  * \param data Pointer to the GtkSatselector widget.
  *
  * This function is called when the user double clicks on a satellite in the
  * satellite selector. It is used to trigger the "sat-activated" signal of
  * the GtkSatSelector widget.
  */
static void row_activated_cb (GtkTreeView *view, GtkTreePath *path,
                              GtkTreeViewColumn *column, gpointer data)
{
    GtkSatSelector   *selector = GTK_SAT_SELECTOR (data);
    GtkTreeSelection *selection;
    GtkTreeModel     *model;
    GtkTreeIter       iter;
    gboolean          haveselection = FALSE; /* this flag is set to TRUE if there is a selection */
    gint              catnum;     /* catalog number of the selected satellite */

    /* get the selected row in the treeview */
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (selector->tree));
    haveselection = gtk_tree_selection_get_selected (selection, &model, &iter);

    if (haveselection) {
        /* get the name and catalog number of the selected saetllite */
        gtk_tree_model_get (model, &iter,
                            GTK_SAT_SELECTOR_COL_CATNUM, &catnum,
                            -1);

        /* emit the "sat-activated" signal for the GtkSatSelector */
        g_signal_emit (G_OBJECT (selector), gtksatsel_signals[SAT_ACTIVATED_SIGNAL], 0, catnum);
    }
}


/** \brief Get information about the selected satellite.
  * \param selector Pointer to the GtkSatSelector widget.
  * \param catnum Location where catnum will be stored (may be NULL).
  * \param satname Location where the satellite name will be stored. May NOT be NULL. Must be g_freed after use.
  * \param epoch Location where the satellite Epoch will be stored (may be NULL);
  *
  * This function can be used to retrieve information about the currently selected satellite
  * a GtkSatSelector widget, e.g. after the "sat-activated" signal has been emitted.
  */
void gtk_sat_selector_get_selected (GtkSatSelector *selector,
                                    gint *catnum, gchar **satname, gdouble *epoch)
{
    GtkTreeSelection *selection;
    GtkTreeModel     *model;
    GtkTreeIter       iter;
    gboolean          haveselection = FALSE; /* this flag is set to TRUE if there is a selection */
    gchar            *l_satname;   /* nickname of the selected satellite */
    gint              l_catnum;     /* catalog number of the selected satellite */
    gdouble           l_epoch;      /* TLE epoch of the selected satellite */

    /* selector can not be NULL */
    g_return_if_fail ((selector != NULL) && (satname != NULL));

    /* get the selected row in the treeview */
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (selector->tree));
    haveselection = gtk_tree_selection_get_selected (selection, &model, &iter);

    if (haveselection) {
        /* get the name and catalog number of the selected saetllite */
        gtk_tree_model_get (model, &iter,
                            GTK_SAT_SELECTOR_COL_NAME, &l_satname,
                            GTK_SAT_SELECTOR_COL_CATNUM, &l_catnum,
                            GTK_SAT_SELECTOR_COL_EPOCH, &l_epoch,
                            -1);

        if (catnum != NULL)
            *catnum = l_catnum;

        *satname = g_strdup (l_satname);

        if (epoch != NULL)
            *epoch = l_epoch;

        g_free (l_satname);
    }

}

