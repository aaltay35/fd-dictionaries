#include "entryedit.h"

// for lookup_widget()
#include "support.h"

// for teidoc
#include "utils.h"

#include "xml.h"

// This feature is useful only if the optionmenus can be left with tab key as
// well. Otherwise is is hard to skip an optionemnu.
//#define OPEN_OPTIONMENUS_ON_FOCUS 0
#undef OPEN_OPTIONMENUS_ON_FOCUS

#ifdef OPEN_OPTIONMENUS_ON_FOCUS
static gboolean open_menu_on_focus(GtkWidget *widget, GtkDirectionType arg1,
                                            gpointer user_data)
{
  //g_printerr("open_menu_on_focus %x: ", widget);

  if(arg1 == GTK_DIR_TAB_FORWARD) g_printerr("GTK_DIR_TAB_FORWARD\n");
  if(arg1 == GTK_DIR_DOWN) g_printerr("GTK_DIR_DOWN\n");
  if(arg1 == GTK_DIR_RIGHT) g_printerr("GTK_DIR_RIGHT\n");

  g_object_set_data(G_OBJECT(widget), "GtkDirectionType",
      GINT_TO_POINTER(arg1));
}


static gboolean open_menu_on_focus_in(GtkWidget *widget, GdkEventFocus *event,
    gpointer user_data)
{
  //g_printerr("open_menu_on_focus_in %x: ", widget);

  GtkDirectionType d = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
	"GtkDirectionType"));
  gboolean will_open = (d==GTK_DIR_TAB_FORWARD);
  g_printerr("will_open=%i d=%i\n", will_open, d);
  
  g_object_set_data(G_OBJECT(widget), "GtkDirectionType",
      GINT_TO_POINTER(-1));

  if(will_open)
  {
    // this should open the optionmenu
    // it emulates a space key press 
    GdkEventKey k;
    k.type = GDK_KEY_PRESS;
    k.window = widget->window;
    k.send_event = 0;
    k.time = 0;
    k.state = 0;
    k.keyval = GDK_space;
    k.length = 0;
    k.string = 0;
    k.hardware_keycode = 0;
    k.group = 0;
    gtk_main_do_event((GdkEvent*)&k);
  } 
  return FALSE;
}
#endif // OPEN_OPTIONMENUS_ON_FOCUS


// returns the value of the Values array that corresponds to index
// returns NULL for out of bounds
static const gchar *index2value(const Values *values, const int index)
{
  g_return_val_if_fail(values, NULL);
  int i = 0;
  // use 'while' even though we can just index into values[],
  // because we don't know the array length
  while(values && i<index && values->value) { values++; i++; }
  if(values && values->value) return values->value;
  return NULL;
}


// returns the index in the Values array that corresponds to value
// returns -1 on unknown value
static int value2index(const Values *values, const gchar *value)
{
  g_return_val_if_fail(values, -1);
  if(!value) return 0;

  int i = 0;
  while(values && values->value)
  {
    if(!strcmp(value, values->value)) return i;
    values++;
    i++;
  }
  return -1;
}


// fills an GtkOptionMenu with entries from a Values array
GtkWidget *create_menu(GtkOptionMenu *parent, const char *accel_path,
    const Values *v)
{
  GnomeUIInfo menu_item[] = 
  {
    {
      GNOME_APP_UI_ITEM, /* Type */
      "None", /* label */
      NULL, /* tooltip */
      (gpointer) NULL, /* Extra information; depends on the type */
      NULL, /* User data sent to the callback */
      NULL, /* reserved */
      GNOME_APP_PIXMAP_NONE, NULL,
      0, /* Accelerator key, or 0 for none */
      (GdkModifierType) 0, /* Mask of modifier keys for the accelerator */
      NULL
    },
    GNOMEUIINFO_END 
  };

  GtkWidget *menu = gtk_menu_new();
  gtk_option_menu_set_menu(parent, menu);
    
  int i = 0;
  while(v->label)
  {
    menu_item[0].label = v->label;
    gnome_app_fill_menu(GTK_MENU_SHELL(menu), menu_item, NULL, TRUE, i);
    v++;
    i++;
  }

  if(accel_path) gtk_menu_set_accel_path(GTK_MENU(menu), accel_path);
  
  return menu;
}


// typology for cross references
const Values xr_values[] = {
  { N_("Undetermined"), "" },
  { N_("Antonym"), "ant" },
  { N_("Hypernym"), "hyper" },
  { N_("Hyponym"), "hypo" },
  { N_("Synonym"), "syn" },
  { N_("Derived from"), "der" },
  NULL
};


// fill dropdown box of combo entry of a cross reference
// with suggestions, ie. headwords from other entries that
// match the existing prefix in the GtkEntry of the combo box
// XXX see gtk-demos for correct entry completion
static void on_xr_combo_dropdown(GtkWidget *widget, gpointer user_data)
{
  int both = GPOINTER_TO_INT(user_data);
  int snr = both & 0xFFFF;
  int xnr = (both >> 16) & 0xFFFF;
  //g_printerr("snr=%i xnr=%i\n", snr, xnr);
  Sense *s = &g_array_index(senses, Sense, snr);
  Sense_xr *x = &g_array_index(s->xr, Sense_xr, xnr);
  
  gchar* select = (gchar*) gtk_entry_get_text(GTK_ENTRY(x->combo_entry));
  if(strlen(select)<2) return;
  
  GList *items = NULL;
  items = g_list_append(items, select);
  
  gchar expr[200];
  g_snprintf(expr, sizeof(expr),
      "/TEI.2/text/body/entry/form/orth[contains(.,'%s')]", select);
  xmlNodeSetPtr nodes = find_node_set(expr, teidoc, NULL);
  
  if(nodes)
  {
    xmlNodePtr *n;
    int i;
    for(i=0, n = nodes->nodeTab; *n && i<nodes->nodeNr; n++, i++)
    {
      xmlChar* content = xmlNodeGetContent(*n);
      items = g_list_append(items, content ? content : (xmlChar*) _("(nothing)"));
    }
    xmlXPathFreeNodeSet(nodes);
  }

  gtk_combo_set_popdown_strings(GTK_COMBO(x->combo), items);
}


// Append empty widgets for a cross reference to a sense. Returns NULL on
// failure, otherwise a pointer to the new Sense_xr struct.
static Sense_xr *sense_append_xr(const Sense *s)
{
  g_return_val_if_fail(s, NULL);
  g_return_val_if_fail(s->xr, NULL);
  g_return_val_if_fail(s->xr_table,NULL);

  Sense_xr xr;
  memset(&xr, 0, sizeof(xr));

  gtk_table_resize(GTK_TABLE(s->xr_table), s->xr->len+1, 2);
 
  // type label for the crossref
  xr.type_optionmenu = gtk_option_menu_new();
  create_menu(GTK_OPTION_MENU(xr.type_optionmenu), NULL, xr_values);
  gtk_table_attach(GTK_TABLE(s->xr_table), xr.type_optionmenu,
     		    0, 1, s->xr->len, s->xr->len+1,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  
  // a change enables the "save entry" button
  g_signal_connect((gpointer) xr.type_optionmenu, "changed",
      G_CALLBACK(on_form_optionmenu_changed), NULL);

  xr.combo = gtk_combo_new();
  gtk_table_attach(GTK_TABLE(s->xr_table), xr.combo,
     		    1, 2, s->xr->len, s->xr->len+1,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  xr.combo_entry = GTK_COMBO(xr.combo)->entry;
  // popwin is private :(
  // XXX the "show" event comes too late :(
  g_signal_connect((gpointer) GTK_COMBO(xr.combo)->popwin, "show",
      G_CALLBACK (on_xr_combo_dropdown),
      GINT_TO_POINTER( ((s->xr->len)<<16) | (s->nr) ) );
  g_signal_connect((gpointer) xr.combo_entry, "changed",
      G_CALLBACK(on_form_entry_changed), NULL);
  
  gtk_widget_show_all(s->xr_table);
  g_array_append_val(s->xr, xr);
  if(s->xr_delete_button)
    gtk_widget_set_sensitive(s->xr_delete_button, s->xr->len);
  
  return &g_array_index(s->xr, Sense_xr, s->xr->len-1);
}


void sense_remove_last_xr(const Sense *s)
{
  g_return_if_fail(s);
  g_return_if_fail(s->xr);
  g_return_if_fail(s->xr->len > 0);
  Sense_xr *xr = &g_array_index(s->xr, Sense_xr, s->xr->len-1);
  g_return_if_fail(xr);
  g_return_if_fail(xr->type_optionmenu);
  g_return_if_fail(xr->combo);
  gtk_widget_destroy(xr->type_optionmenu);
  gtk_widget_destroy(xr->combo);
  
  g_array_remove_index_fast(s->xr, s->xr->len-1);

  if(s->xr->len == 0)
  {
    // show label "No cross-references exist."
    // XXX care for this label when creating a new sense and adding the first xr
    GtkWidget *l = gtk_label_new(_("No Cross-References exist."));
    gtk_table_attach(GTK_TABLE(s->xr_table), l, 0, 1, 0, 2,
	(GtkAttachOptions) (GTK_FILL),
	(GtkAttachOptions) (0), 0, 0);
    //gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_LEFT);
    //gtk_misc_set_alignment(GTK_MISC(l), 0, 0.5);
  }
  else gtk_table_resize(GTK_TABLE(s->xr_table), s->xr->len, 2);

  if(s->xr_delete_button)
    gtk_widget_set_sensitive(s->xr_delete_button, s->xr->len);
}


static void
on_xr_add_button_clicked        (GtkButton       *button,
                                 gpointer         user_data)
{ 
  int n = GPOINTER_TO_INT(user_data);
  Sense *s = &g_array_index(senses, Sense, n);
  gtk_widget_grab_focus(sense_append_xr(s)->combo_entry);
  on_form_entry_changed(NULL, NULL);
}


static void
on_xr_delete_button_clicked        (GtkButton       *button,
                                    gpointer         user_data)
{ 
  int n = GPOINTER_TO_INT(user_data);
  Sense *s = &g_array_index(senses, Sense, n);
  sense_remove_last_xr(s);
  on_form_entry_changed(NULL, NULL);
}


// XXX these should be configurable
// but how to load/save them easily while staying(?) portable? is gconf portable?
const Values pos_values[] = {
  { N_("None"), "" },
  { N_("Verb"), "v" },
  { N_("Transitive Verb"), "vt" },
  { N_("Intransitive Verb"), "vi" },
  { N_("Adverb"), "adv" },
  { N_("_Noun"), "n" },
  { N_("Pronoun"), "pron" },
  { N_("_Adjective"), "adj" },
  { N_("_Preposition"), "prep" },
  { N_("Conjunction"), "conj" },
  { N_("_Interjection"), "interj" },
  { N_("Imitative"), "imit" },
  { N_("Abbreviation"), "abbr" },
  { N_("Phrase"), "phra" },
  NULL
};

const Values gen_values[] = {
  { N_("None"), "" },
  { N_("Masculine"), "m" },
  { N_("_Feminine"), "f" },
  { N_("Neuter"), "n" },
  { N_("Common"), "i" },
  { N_("Masc. & Fem."), "mf" },
  { N_("Masc., Fem. & Neut."), "mfn" },
  NULL
};


const Values num_values[] = {
  { N_("None"), "" },
  { N_("_Singular"), "sg" },
  { N_("Dual"), "du" },
  { N_("Plural"), "pl" },
  NULL
};

const Values domain_values[] = {
  { "None", "" },

  // in German: "Sachgebiete"
  // taken from fdicts.com
  { N_("Agriculture"), "agr" },
  { N_("Astronomy"), "astr" },
  { N_("Automobile"), "aut" },
  { N_("_Biology"), "bio" },
  { N_("Botany"), "bot" },
  { N_("Chemistry"), "chem" },
  { N_("Electrotechnics"), "el" },
  { N_("Finance"), "fin" },
  { N_("Geography"), "geo" },
  { N_("Geology"), "geol" },
  { N_("Grammar"), "gram" },
  { N_("History"), "hist" },
  { N_("Information Technology"), "it" },
  { N_("Law"), "law" },
  { N_("Mathematics"), "math" },
  { N_("Medicine"), "med" },
  { N_("Military"), "mil" },
  { N_("Music"), "mus" },
  { N_("Mythology"), "myt" },
  { N_("Physics"), "phy" },
  { N_("Politics"), "pol" },
  { N_("Religion"), "rel" },
  { N_("Sexual"), "sex" },
  { N_("Sport"), "sport" },
  { N_("_Technology"), "tech" },

  // XXX these are a bit different and should have
  // usg[@type='reg'] for "register"
  // TEI 12.3.5.2 Usage Information and Other Labels
  // suggests slang, formal, taboo, ironic, facetious
//  { "_Official", "official" },
//  { "_Formal", "formal" }, // same as official?
//  { "Children Speech", "chil" },
//  { "Colloquial", "col" },
//  { "Slang", "slang" },// same as colloquial?
//  { "Vulgar", "vulg" },// same as colloquial?
  NULL
};


// appends translation equivalent input fields to a sense of an entry
Sense_trans *sense_append_trans(const Sense *s)
{
  g_return_val_if_fail(s, NULL);
  g_return_val_if_fail(s->trans, NULL);
  g_return_val_if_fail(s->tr_vbox, NULL);

  Sense_trans t;
  memset(&t, 0, sizeof(t));
  
  t.hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start_defaults(GTK_BOX(s->tr_vbox), t.hbox);

  t.entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(t.hbox), t.entry, TRUE, TRUE, 0);
  g_signal_connect((gpointer) t.entry, "changed",
      G_CALLBACK(on_form_entry_changed), NULL);
  
  GtkTooltips *tooltips;
  tooltips = GTK_TOOLTIPS(lookup_widget(app1, "tooltips"));
  gtk_tooltips_set_tip(tooltips, t.entry, _("Translation Equivalent"), NULL);

  // pos
  t.pos_optionmenu = gtk_option_menu_new();
  gtk_box_pack_start(GTK_BOX(t.hbox), t.pos_optionmenu, FALSE, FALSE, 0);
  gtk_tooltips_set_tip(tooltips, t.pos_optionmenu, _("Part of Speech"), NULL);
  create_menu(GTK_OPTION_MENU(t.pos_optionmenu), NULL, pos_values);
  g_signal_connect((gpointer) t.pos_optionmenu, "changed",
      G_CALLBACK(on_form_optionmenu_changed), NULL);
#ifdef OPEN_OPTIONMENUS_ON_FOCUS
  g_signal_connect((gpointer) t.pos_optionmenu, "focus-in-event",
      G_CALLBACK(open_menu_on_focus_in), NULL);
  g_signal_connect((gpointer) t.pos_optionmenu, "focus",
      G_CALLBACK(open_menu_on_focus), NULL);
#endif
  
  // gen
  t.gen_optionmenu = gtk_option_menu_new();
  gtk_box_pack_start(GTK_BOX(t.hbox), t.gen_optionmenu, FALSE, FALSE, 0);
  gtk_tooltips_set_tip(tooltips, t.gen_optionmenu, _("Genus"), NULL);
  create_menu(GTK_OPTION_MENU(t.gen_optionmenu), NULL, gen_values);
  g_signal_connect((gpointer) t.gen_optionmenu, "changed",
      G_CALLBACK(on_form_optionmenu_changed), NULL);
#ifdef OPEN_OPTIONMENUS_ON_FOCUS
  g_signal_connect((gpointer) t.gen_optionmenu, "focus-in-event",
      G_CALLBACK(open_menu_on_focus_in), NULL);
  g_signal_connect((gpointer) t.gen_optionmenu, "focus",
      G_CALLBACK(open_menu_on_focus), NULL);
#endif

  gtk_widget_show_all(s->tr_vbox);
  g_array_append_val(s->trans, t);

  if(s->tr_delete_button)
    gtk_widget_set_sensitive(s->tr_delete_button, s->trans->len);
  return &g_array_index(s->trans, Sense_trans, s->trans->len-1);
}


// chop the last trans widgets
static void sense_remove_last_trans(const Sense *s)
{
  g_return_if_fail(s);
  g_return_if_fail(s->trans);
  g_return_if_fail(s->trans->len > 0);
  Sense_trans *t = &g_array_index(s->trans, Sense_trans,
      s->trans->len-1);
  g_return_if_fail(t);
  g_return_if_fail(t->hbox);

  gtk_widget_destroy(t->hbox);
  g_array_remove_index_fast(s->trans, s->trans->len-1);
  if(s->tr_delete_button)
    gtk_widget_set_sensitive(s->tr_delete_button, s->trans->len);
}


static void 
on_tr_add_button_clicked        (GtkButton       *button,
                                 gpointer         user_data)
{ 
  const int n = GPOINTER_TO_INT(user_data);
  Sense *s = &g_array_index(senses, Sense, n);
  Sense_trans *t = sense_append_trans(s);
  gtk_widget_grab_focus(t->entry);
  on_form_entry_changed(NULL, NULL);
}


static void
on_tr_delete_button_clicked        (GtkButton       *button,
                                    gpointer         user_data)
{ 
  int n = GPOINTER_TO_INT(user_data);
  Sense *s = &g_array_index(senses, Sense, n);
  sense_remove_last_trans(s);
  on_form_entry_changed(NULL, NULL);
}


// Adds empty widgets for another sense to the currently edited entry.
// Initially there are no widgets for translation equivalents. They can be
// added with sense_append_trans(). Returns a pointer to the newly created
// sense.
Sense *senses_append(GArray *senses)
{
  g_return_if_fail(senses);

  Sense s;
  memset(&s, 0, sizeof(s));
  
  s.frame = gtk_frame_new(NULL);

  // all senses are kept in vbox6
  // the "add/remove sense" buttonbox is always last in vbox6, since it has
  // "pack_start" set to false
  GtkBox *vbox6 = GTK_BOX(lookup_widget(app1, "vbox6"));
  gtk_box_pack_start(vbox6, s.frame, TRUE, TRUE, 0);

  // sense number label
  char l[30];
  snprintf(l, sizeof(l), _("Sense %i"), senses->len+1);
  s.label = gtk_label_new(l);
  gtk_frame_set_label_widget(GTK_FRAME(s.frame), s.label);

  // use a table for aligning all input fields nicely
  s.table = gtk_table_new(7, 3, FALSE);
  gtk_container_add(GTK_CONTAINER(s.frame), s.table);

  // domain label
  s.domain_label = gtk_label_new(_("Domain"));
  gtk_table_attach(GTK_TABLE(s.table), s.domain_label, 0, 1, 0, 1,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);

  GtkTooltips *tooltips;
  tooltips = GTK_TOOLTIPS(lookup_widget(app1, "tooltips"));
  
  // domain optionmenu
  s.domain_optionmenu = gtk_option_menu_new();
  gtk_table_attach(GTK_TABLE(s.table), s.domain_optionmenu, 1, 3, 0, 1,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  create_menu(GTK_OPTION_MENU(s.domain_optionmenu), NULL, domain_values);
  // "changed" callback will enable "save entry" button
  g_signal_connect((gpointer) s.domain_optionmenu, "changed",
      G_CALLBACK(on_form_optionmenu_changed), NULL);
#ifdef OPEN_OPTIONMENUS_ON_FOCUS
  g_signal_connect((gpointer) s.domain_optionmenu, "focus-in-event",
      G_CALLBACK(open_menu_on_focus_in), NULL);
#endif
  gtk_tooltips_set_tip(tooltips, s.domain_optionmenu, _("Domain of use"), NULL);

  s.trans = g_array_new(FALSE, TRUE, sizeof(Sense_trans));

  // translations label
  s.tr_label = gtk_label_new(_("Translation(s)"));
  gtk_table_attach(GTK_TABLE(s.table), s.tr_label, 0, 1, 1, 2,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_label_set_justify(GTK_LABEL(s.tr_label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment(GTK_MISC(s.tr_label), 0, 0.5);

  // a vbox to hold all trans hboxes 
  s.tr_vbox = gtk_vbox_new(FALSE, 0);
  gtk_table_attach(GTK_TABLE(s.table), s.tr_vbox, 1, 2, 1, 2,
                    (GtkAttachOptions) (GTK_EXPAND),
                    (GtkAttachOptions) (GTK_FILL), 0, 0);

  // add/remove trans buttonbox
  s.tr_hbuttonbox = gtk_hbutton_box_new();
  gtk_table_attach(GTK_TABLE (s.table), s.tr_hbuttonbox, 2, 3, 1, 2,
                    (GtkAttachOptions) (GTK_EXPAND),
                    (GtkAttachOptions) (GTK_FILL), 0, 0);
  gtk_button_box_set_layout(GTK_BUTTON_BOX (s.tr_hbuttonbox),
      GTK_BUTTONBOX_SPREAD);

  // add trans button
  s.tr_add_button = gtk_button_new();
  gtk_container_add(GTK_CONTAINER(s.tr_hbuttonbox), s.tr_add_button);
  GTK_WIDGET_SET_FLAGS(s.tr_add_button, GTK_CAN_DEFAULT);

  // Hand over the sense number, since the other senses will also have add/rem
  // buttons.  Do not hand over a pointer to an array element or the local
  // variable s, as their addresses can change!
  g_signal_connect((gpointer) s.tr_add_button, "clicked",
      G_CALLBACK(on_tr_add_button_clicked), GINT_TO_POINTER(senses->len));
  gtk_tooltips_set_tip(tooltips, s.tr_add_button,
      _("Add new Translation Equivalent"), NULL);
  
  s.tr_add_alignment = gtk_alignment_new(0.5, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(s.tr_add_button), s.tr_add_alignment);

  s.tr_add_hbox = gtk_hbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(s.tr_add_alignment), s.tr_add_hbox);

  s.tr_add_image = gtk_image_new_from_stock("gtk-add", GTK_ICON_SIZE_BUTTON);
  gtk_box_pack_start(GTK_BOX(s.tr_add_hbox), s.tr_add_image, FALSE, FALSE, 0);

  s.tr_add_label = gtk_label_new_with_mnemonic("Add");
  gtk_box_pack_start(GTK_BOX(s.tr_add_hbox), s.tr_add_label, FALSE, FALSE, 0);
  gtk_label_set_justify(GTK_LABEL(s.tr_add_label), GTK_JUSTIFY_LEFT);

  // remove trans button
  s.tr_delete_button = gtk_button_new ();
  gtk_container_add(GTK_CONTAINER(s.tr_hbuttonbox), s.tr_delete_button);
  GTK_WIDGET_SET_FLAGS(s.tr_delete_button, GTK_CAN_DEFAULT);
  g_signal_connect((gpointer) s.tr_delete_button, "clicked",
      G_CALLBACK(on_tr_delete_button_clicked), GINT_TO_POINTER(senses->len));
  gtk_tooltips_set_tip(tooltips, s.tr_delete_button,
      _("Remove last Translation Equivalent"), NULL);
  gtk_widget_set_sensitive(s.tr_delete_button, FALSE);

  s.tr_delete_alignment = gtk_alignment_new(0.5, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(s.tr_delete_button), s.tr_delete_alignment);

  s.tr_delete_hbox = gtk_hbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(s.tr_delete_alignment), s.tr_delete_hbox);

  s.tr_delete_image = gtk_image_new_from_stock("gtk-remove",
      GTK_ICON_SIZE_BUTTON);
  gtk_box_pack_start(GTK_BOX(s.tr_delete_hbox), s.tr_delete_image,
      FALSE, FALSE, 0);

  s.tr_delete_label = gtk_label_new_with_mnemonic("Remove");
  gtk_box_pack_start(GTK_BOX(s.tr_delete_hbox), s.tr_delete_label, 
      FALSE, FALSE, 0);
  gtk_label_set_justify(GTK_LABEL(s.tr_delete_label), GTK_JUSTIFY_LEFT);

  // def
  s.def_label = gtk_label_new("Definition");
  gtk_table_attach(GTK_TABLE(s.table), s.def_label, 0, 1, 2, 3,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_label_set_justify(GTK_LABEL(s.def_label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment(GTK_MISC(s.def_label), 0, 0.5);

  s.def_entry = gtk_entry_new();
  gtk_table_attach(GTK_TABLE(s.table), s.def_entry, 1, 3, 2, 3,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  g_signal_connect((gpointer) s.def_entry, "changed",
      G_CALLBACK(on_form_entry_changed), NULL);
  gtk_tooltips_set_tip(tooltips, s.def_entry,
      _("Definition of this Sense"), NULL);

  // note
  s.note_label = gtk_label_new("Note");
  gtk_table_attach(GTK_TABLE(s.table), s.note_label, 0, 1, 3, 4,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_label_set_justify(GTK_LABEL(s.note_label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment(GTK_MISC(s.note_label), 0, 0.5);

  s.note_entry = gtk_entry_new();
  gtk_table_attach(GTK_TABLE(s.table), s.note_entry, 1, 3, 3, 4,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  g_signal_connect((gpointer) s.note_entry, "changed",
      G_CALLBACK(on_form_entry_changed), NULL);
  gtk_tooltips_set_tip(tooltips, s.note_entry, _("Freestyle Note"), NULL);

  // xr label
  s.xr = g_array_new(FALSE, TRUE, sizeof(Sense_xr));

  s.xr_label = gtk_label_new("Cross-References");
  gtk_table_attach(GTK_TABLE(s.table), s.xr_label, 0, 1, 4, 5,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_label_set_justify(GTK_LABEL (s.xr_label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment (GTK_MISC (s.xr_label), 0, 0.5);

  // a table to hold widgets for all xrs
  s.xr_table = gtk_table_new(0, 2, FALSE);
  gtk_table_attach(GTK_TABLE(s.table), s.xr_table, 1, 2, 4, 5,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  
  // xr buttonbox
  s.xr_hbuttonbox = gtk_hbutton_box_new();
  gtk_table_attach(GTK_TABLE(s.table), s.xr_hbuttonbox, 2, 3, 4, 5,
                    (GtkAttachOptions) (0),
                    (GtkAttachOptions) (GTK_FILL), 0, 0);

  // add xr
  s.xr_add_button = gtk_button_new();
  gtk_container_add(GTK_CONTAINER(s.xr_hbuttonbox), s.xr_add_button);
  GTK_WIDGET_SET_FLAGS(s.xr_add_button, GTK_CAN_DEFAULT);
  g_signal_connect((gpointer) s.xr_add_button, "clicked",
      G_CALLBACK(on_xr_add_button_clicked), GINT_TO_POINTER(senses->len));
  gtk_tooltips_set_tip(tooltips, s.xr_add_button,
      _("Add new Cross-Reference to another Headword in this Dictionary"),
      NULL);

  s.xr_add_alignment = gtk_alignment_new(0.5, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(s.xr_add_button), s.xr_add_alignment);

  s.xr_add_hbox = gtk_hbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(s.xr_add_alignment), s.xr_add_hbox);

  s.xr_add_image = gtk_image_new_from_stock("gtk-add", GTK_ICON_SIZE_BUTTON);
  gtk_box_pack_start(GTK_BOX(s.xr_add_hbox), s.xr_add_image, FALSE, FALSE, 0);

  s.xr_add_label = gtk_label_new_with_mnemonic("Add");
  gtk_box_pack_start(GTK_BOX(s.xr_add_hbox), s.xr_add_label, FALSE, FALSE, 0);
  gtk_label_set_justify(GTK_LABEL(s.xr_add_label), GTK_JUSTIFY_LEFT);

  // remove xr
  s.xr_delete_button = gtk_button_new();
  gtk_container_add(GTK_CONTAINER(s.xr_hbuttonbox), s.xr_delete_button);
  GTK_WIDGET_SET_FLAGS(s.xr_delete_button, GTK_CAN_DEFAULT);
  g_signal_connect((gpointer) s.xr_delete_button, "clicked",
      G_CALLBACK(on_xr_delete_button_clicked), GINT_TO_POINTER(senses->len));
  gtk_tooltips_set_tip(tooltips, s.xr_delete_button,
      _("Remove last Cross-Reference"), NULL);
  gtk_widget_set_sensitive(s.xr_delete_button, FALSE);

  s.xr_delete_alignment = gtk_alignment_new(0.5, 0.5, 0, 0);
  gtk_container_add(GTK_CONTAINER(s.xr_delete_button), s.xr_delete_alignment);

  s.xr_hbox = gtk_hbox_new(FALSE, 2);
  gtk_container_add(GTK_CONTAINER(s.xr_delete_alignment), s.xr_hbox);

  s.xr_delete_image = gtk_image_new_from_stock("gtk-remove",
      GTK_ICON_SIZE_BUTTON);
  gtk_box_pack_start(GTK_BOX(s.xr_hbox), s.xr_delete_image, FALSE, FALSE, 0);

  s.xr_delete_label = gtk_label_new_with_mnemonic("Remove");
  gtk_box_pack_start(GTK_BOX(s.xr_hbox), s.xr_delete_label, FALSE, FALSE, 0);
  gtk_label_set_justify(GTK_LABEL(s.xr_delete_label), GTK_JUSTIFY_LEFT);

  // example
  s.example_label = gtk_label_new(_("Example"));
  gtk_table_attach(GTK_TABLE(s.table), s.example_label, 0, 1, 6, 7,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_label_set_justify(GTK_LABEL(s.example_label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment(GTK_MISC(s.example_label), 0, 0.5);

  s.example_entry = gtk_entry_new();
  gtk_table_attach(GTK_TABLE(s.table), s.example_entry, 1, 3, 6, 7,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  g_signal_connect((gpointer) s.example_entry, "changed",
      G_CALLBACK(on_form_entry_changed), NULL);
  gtk_tooltips_set_tip(tooltips, s.example_entry,
      _("Example phrase or sentence where the Headword is used in this Sense"),
      NULL);

  s.example_tr_entry = gtk_entry_new();
  gtk_table_attach(GTK_TABLE(s.table), s.example_tr_entry, 1, 3, 7, 8,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  g_signal_connect((gpointer) s.example_tr_entry, "changed",
      G_CALLBACK(on_form_entry_changed), NULL);
  gtk_tooltips_set_tip(tooltips, s.example_tr_entry,
      _("Optional Translation of the Example"),
      NULL);

  s.nr = senses->len;

  // Since vbox6 is inside a viewport, adding a widget extends the viewport
  // area, but we do not want that.  I don't know whether GtkViewport exhibits
  // a bug when gtk_widget_queue_resize() is not called, since even though it
  // seems to get extended, its scrollbars are not shown.
  gtk_widget_queue_resize(lookup_widget(app1, "viewport2"));

  gtk_widget_show_all(s.frame);
  g_array_append_val(senses, s);

  GtkWidget *remove_sense_button = lookup_widget(app1, "remove_sense_button");
  gtk_widget_set_sensitive(remove_sense_button, TRUE);

  // return newly created Sense
  return &g_array_index(senses, Sense, senses->len-1);
}


void senses_remove_last(GArray *senses)
{
  g_return_if_fail(senses && senses->len > 0);

  // XXX does this assignment copy the Sense struct?
  // look into C reference/standard or debug!
  Sense s = g_array_index(senses, Sense, senses->len-1);
  g_assert(s.frame);
  // will also remove it from its container, as well as destroy all
  // child widgets
  gtk_widget_destroy(s.frame);
 
  g_array_free(s.trans, TRUE);

  // since we always remove the last element, the fast function
  // is fine
  g_array_remove_index_fast(senses, senses->len-1);
  GtkWidget *remove_sense_button = lookup_widget(app1, "remove_sense_button");
  g_assert(remove_sense_button);
  gtk_widget_set_sensitive(remove_sense_button, senses->len);
}

static void senses_clear(GArray *senses)
{
  g_return_if_fail(senses);
  while(senses->len > 0) senses_remove_last(senses);
}

static void nodeContent2gtk_entry(const xmlNodePtr n, GtkEntry *e)
{
  g_return_if_fail(e);
  if(!n)
  {
    gtk_entry_set_text(e, "");
    return;
  }
  xmlChar* content = xmlNodeGetContent(n);
  gtk_entry_set_text(e, content ? content : ((xmlChar*) ""));
  if(content) xmlFree(content);
}


// returns whether the nodeContent could be put into the
// optionmenu successfully
static gboolean nodeContent2optionmenu(const xmlNodePtr n, GtkOptionMenu *om,
    const Values *values, const char *errormsg)
{
  g_return_val_if_fail(om, FALSE); 
  xmlChar* content = xmlNodeGetContent(n);
  guint index_ = value2index(values, content);
  if(index_==-1)
  {
    g_printerr(_("Unknown <%s> content: '%s'\n"), errormsg, content);
    if(content) xmlFree(content);
    gtk_option_menu_set_history(om, 0);
    return FALSE;
  }
  if(content) xmlFree(content);
  gtk_option_menu_set_history(om, index_);
  return TRUE;
}


/** Frees xmlNodePtr p, if it is non-NULL. Nullifies it after freeing.
 */
void my_free_node(xmlNodePtr *p)
{
  g_return_if_fail(p);
  if(!*p) return;
  xmlFreeNode(*p);
  *p = 0;
}


/** Extracts contents of xml nodes and fills input fields with them.
 * @n: zero-based
 * returns whether extraction was successful (it may fail for invalid values
 * for the optionmenus)
 */
static gboolean sense_dom2widgets(const GArray *senses, const int n)
{
  g_return_val_if_fail(senses, FALSE);
  g_return_val_if_fail(n < senses->len, FALSE);
  Sense *s = &g_array_index(senses, Sense, n);
  g_return_val_if_fail(s, FALSE);
  g_return_val_if_fail(s->trans, FALSE);
  //g_printerr("sense_dom2widgets senses.len=%i n=%i\n", senses.len, n);

  // domain  
  gboolean can = nodeContent2optionmenu(s->xUsg,
      GTK_OPTION_MENU(s->domain_optionmenu), domain_values, "usg type=\"dom\"");
  my_free_node(&(s->xUsg));

  // for all trans
  int i;
  for(i=0; i < s->trans->len; i++)
  {
    // tr
    Sense_trans *t = &g_array_index(s->trans, Sense_trans, i);
    nodeContent2gtk_entry(t->xTr, GTK_ENTRY(t->entry));
    my_free_node(&(t->xTr));

    // pos of trans should always be pos of headword, because
    // as per convention homographs should be put into different
    // headwords
    can = can &&
      nodeContent2optionmenu(t->xPos, GTK_OPTION_MENU(t->pos_optionmenu),
	  pos_values, "pos");
    my_free_node(&(t->xPos));

    // gen
    can = can && nodeContent2optionmenu(t->xGen,
	GTK_OPTION_MENU(t->gen_optionmenu), gen_values, "gen");
    my_free_node(&(t->xGen));
  }
  
  // note, def
  nodeContent2gtk_entry(s->xNote, GTK_ENTRY(s->note_entry));
  my_free_node(&(s->xNote));
  nodeContent2gtk_entry(s->xDef, GTK_ENTRY(s->def_entry));
  my_free_node(&(s->xDef));

  // ex & its translation
  nodeContent2gtk_entry(s->xEx, GTK_ENTRY(s->example_entry));
  my_free_node(&(s->xEx));
  nodeContent2gtk_entry(s->xExTr, GTK_ENTRY(s->example_tr_entry));
  my_free_node(&(s->xExTr));
  
  // for all xr
  for(i=0; i < s->xr->len; i++)
  {
    // xr
    Sense_xr *xr = &g_array_index(s->xr, Sense_xr, i);
    nodeContent2gtk_entry(xr->xRef, GTK_ENTRY(xr->combo_entry));
    my_free_node(&(xr->xRef));
    // xr type
    if(xr->xType)
      can = nodeContent2optionmenu(xr->xType,
	GTK_OPTION_MENU(xr->type_optionmenu), xr_values, "xr @type");
    my_free_node(&(xr->xType));
  }

  return can;
}


struct Parsed_entry
{
  xmlNodePtr orth, pron, pos, num, gen, noteRespTranslator;
};


/** Fill main form fields, parse optionmenu contents. Fields of the senses
 * of the entry are filled by sense_dom2widgets().
 */
static void parsed_entry2widgets(struct Parsed_entry *pe, gboolean *can)
{
  g_return_if_fail(pe && can);
  
  // orth
  nodeContent2gtk_entry(pe->orth, GTK_ENTRY(lookup_widget(app1, "entry1")));
  my_free_node(&(pe->orth));
  
  // pron
  nodeContent2gtk_entry(pe->pron, GTK_ENTRY(lookup_widget(app1, "entry2")));
  my_free_node(&(pe->pron));

  *can = *can && nodeContent2optionmenu(pe->pos,
      GTK_OPTION_MENU(lookup_widget(app1, "pos_optionmenu")), pos_values, "pos");
  my_free_node(&(pe->pos));

  *can = *can && nodeContent2optionmenu(pe->num,
      GTK_OPTION_MENU(lookup_widget(app1, "num_optionmenu")), num_values, "num");
  my_free_node(&(pe->num));
  
  *can = *can && nodeContent2optionmenu(pe->gen,
      GTK_OPTION_MENU(lookup_widget(app1, "gen_optionmenu")), gen_values, "gen");
  my_free_node(&(pe->gen));
  
  // parse '<note resp="translator">Translator Name <email address>[two spaces]date</note>'
  // the contents of this <note> are similar to the last line of a debian changelog entry
  if(!pe->noteRespTranslator) return;

  xmlChar *content = xmlNodeGetContent(pe->noteRespTranslator);
  my_free_node(&(pe->noteRespTranslator));
  if(!content) return;
  char *nameS = NULL, *emailS = NULL;
  char dateS[100];
  if(strlen(content)>0)
  {
    // %as =  match a string, malloc it
    // &a[^>] = match a string, malloc it, all chars allowed except '>'
    // XXX might not be robust
    int ret = sscanf(content, "%a[^<]<%a[^>]>  %[a-zA-Z0-9 .,-]",
	&nameS, &emailS, &dateS);

    if(ret != 3)
    {
      *can = FALSE;
      g_printerr("sscanf() = %i\n", ret);
    }
    else
    {
      // remove trailing space from name
      nameS = g_strchomp(nameS);
	
      GDate date;
      //g_date_clear(&date, 1); 

      g_date_set_parse(&date, dateS);
      *can = *can && g_date_valid(&date);
      if(!*can) g_printerr("Invalid date: '%s'\n", dateS);
    }
  }
  else
  {
    *can = FALSE;
  }

  if(!*can)
    g_printerr("note resp=translator: content='%s' name='%s' "
	"email='%s' dateS='%s'\n", content, nameS, emailS, dateS);
  else
  {
    // XXX save found things
  }
  if(nameS) free(nameS);
  if(emailS) free(emailS);
  if(content) xmlFree(content);
}

/** Checks whether @entry is editable in our form. This presupposes is possible
 * only if @entry has only elements/attributes that we can handle with our
 * form. We check this condition by first removing every node from the entry
 * tree that we handle (first attributes, then elements). Then we check whether
 * nothing remains from the tree. Returns whether parsing the entry tree was
 * successful.
 */
gboolean xml2form(const xmlNodePtr entry, GArray *senses)
{
  g_return_val_if_fail(entry && senses, FALSE);
  
  gboolean can = TRUE;// whether parsing the entry was successful
  xmlDocPtr entry_doc = copy_node_to_doc(entry);

  // abbreviate the coming source
  // in the spirit of C++'s design avoid macros are avoided
  xmlNodePtr inline my_unlink(const char *xpath)
  {
    return unlink_leaf_node_with_attr(xpath, NULL, entry_doc, &can);
  }
  
  struct Parsed_entry pe;
  memset(&pe, 0, sizeof(pe));
  
  pe.orth = my_unlink("/entry/form/orth[1]");
  pe.pron = my_unlink("/entry/form/pron[1]");
  
  // <form> should be empty now and without attribute nodes
  xmlNodePtr f = unlink_leaf_node_with_attr("/entry/form", NULL, entry_doc, &can);
  if(f) xmlFreeNode(f);

  if(find_single_node("/entry/gramGrp[1]", entry_doc))
  {
    pe.pos = my_unlink("/entry/gramGrp/pos");
    pe.num = my_unlink("/entry/gramGrp/num");
    pe.gen = my_unlink("/entry/gramGrp/gen");
    f = unlink_leaf_node_with_attr("/entry/gramGrp", NULL, entry_doc, &can);
    if(f) xmlFreeNode(f);
  }

  senses_clear(senses);

  // simple entry format: only 1 trans (with upto 2 trs) ->
  // transform into single sense and 2 trans
  if(find_single_node("/entry/trans[1]", entry_doc))
  {
    //g_printerr("Simple Entry...\n");
    Sense *s = senses_append(senses);
    Sense_trans *t = sense_append_trans(s);
    t->xTr  = my_unlink("/entry/trans[1]/tr[1]");
    t->xGen = my_unlink("/entry/trans[1]/gen[1]");

    // second tr
    if(find_single_node("/entry/trans[1]/tr[1]", entry_doc))
    {
      t = sense_append_trans(s); 
      t->xTr = my_unlink("/entry/trans[1]/tr[1]");
    }

    f = my_unlink("/entry/trans[1]");
    if(f) xmlFreeNode(f);
    sense_dom2widgets(senses, 0);
  }
  else
  {
    // complex entry format: many senses, many trans subelements
    //g_printerr("Complex Entry...\n");
    while(can && find_single_node("/entry/sense[1]", entry_doc))
    {
      // parse a sense
      Sense *s = senses_append(senses);

      // domain    
      const char *allowedattrs[] = { "type", NULL };
      s->xUsg = unlink_leaf_node_with_attr("/entry/sense[1]/usg[1][@type='dom']",
	  allowedattrs, entry_doc, &can);
      
      // for all trans
      while(can && find_single_node("/entry/sense[1]/trans[1]", entry_doc))
      {
	// parse a trans
        Sense_trans *t = sense_append_trans(s);
	g_assert(t);
        t->xTr  = my_unlink("/entry/sense[1]/trans[1]/tr[1]");
        t->xGen = my_unlink("/entry/sense[1]/trans[1]/gen[1]");
        t->xPos = my_unlink("/entry/sense[1]/trans[1]/pos[1]");
        f = my_unlink("/entry/sense[1]/trans[1]");
	if(f) xmlFreeNode(f);
      } // while trans

      // def
      s->xDef = my_unlink("/entry/sense[1]/def[1]");

      // note
      s->xNote = my_unlink("/entry/sense[1]/note[1]");

      // ex (quote and translation)
      s->xEx   = my_unlink("/entry/sense[1]/eg[1]/q[1]");
      s->xExTr = my_unlink("/entry/sense[1]/eg[1]/trans[1]/tr[1]");
      my_unlink("/entry/sense[1]/eg[1]/trans[1]");
      my_unlink("/entry/sense[1]/eg[1]");

      // xr
      while(can && find_single_node("/entry/sense[1]/xr[1]", entry_doc))
      {
	// parse a xr
        Sense_xr *xr = sense_append_xr(s);
	xr->xRef = my_unlink("/entry/sense[1]/xr[1]/ref[1]");
	// @type
	xr->xType = my_unlink("/entry/sense[1]/xr[1]/@type");
	f = my_unlink("/entry/sense[1]/xr[1]");
	if(f) xmlFreeNode(f);
      }
      
      f = my_unlink("/entry/sense[1]");
      if(f) xmlFreeNode(f);
      can = can && sense_dom2widgets(senses, senses->len-1);
    } // while sense
    //g_printerr("Finished parsing complex entry\n");
  } // complex entry
 
  pe.noteRespTranslator = unlink_leaf_node_with_attr("/entry/note[@resp='translator'][1]",
      NULL, entry_doc, &can);
  
  f = unlink_leaf_node_with_attr("/entry", NULL, entry_doc, &can);
  if(f) xmlFreeNode(f);

  // fill main Widgets
  parsed_entry2widgets(&pe, &can);

  // doc was successfully parsed if root element was unlinked
  if(xmlDocGetRootElement(entry_doc))
  {
    // give useful feedback
    xmlBufferPtr buf = xmlBufferCreate();
    int ret2 = xmlNodeDump(buf, entry_doc, xmlDocGetRootElement(entry_doc), 0, 1);
    g_assert(ret2 != -1);    
    g_printerr(_("Remaining content in entry: '%s'.\n"), xmlBufferContent(buf));
    xmlBufferFree(buf); 
    can = FALSE;
  }
  
  // free all nodes
  if(entry_doc) xmlFreeDoc(entry_doc);

  return can;
}


static xmlNodePtr GtkEntry2xmlNode(const xmlNodePtr parent, const gchar *before, const gchar *name,
    GtkEntry *e, const gchar *after)
{
  g_return_if_fail(name);

  const gchar *select = e ? gtk_entry_get_text(e) : NULL;
  if(!strlen(select)) return NULL;

  xmlNodeAddContent(parent, before); 
  xmlNodePtr newNode = xmlNewChild(parent, NULL, name, select);
  xmlNodeAddContent(parent, after); 

  return newNode;
}


// build XML entry as xmlNode with children from fields
// XXX tried as xmlDocFragment before, hoping that entities
// from the DTD would be usable or validation was done, but
// it didn't work out
xmlNodePtr form2xml(const GArray *senses)
{
  g_return_if_fail(teidoc);

  xmlNodePtr entryNode = xmlNewDocNode(teidoc, NULL, "entry", "\n");
  xmlNodePtr formNode = string2xmlNode(entryNode, "  ", "form", "\n", "\n");

  // orth
  GtkEntry2xmlNode(formNode, "    ", "orth",
      GTK_ENTRY(lookup_widget(app1, "entry1")), "\n");

  // pron
  GtkEntry2xmlNode(formNode, "    ", "pron",
      GTK_ENTRY(lookup_widget(app1, "entry2")), "\n");
  xmlNodeAddContent(formNode, "  "); 

  // gramGrp
  gint pindex = gtk_option_menu_get_history(GTK_OPTION_MENU(
	lookup_widget(app1, "pos_optionmenu")));
  gint gindex = gtk_option_menu_get_history(GTK_OPTION_MENU(
	lookup_widget(app1, "gen_optionmenu")));
  gint nindex = gtk_option_menu_get_history(GTK_OPTION_MENU(
	lookup_widget(app1, "num_optionmenu")));
  if(pindex || gindex || nindex)
  {
    xmlNodePtr gramGrpNode = string2xmlNode(entryNode, "  ", "gramGrp", "\n    ", "\n");
    if(pindex)
      string2xmlNode(gramGrpNode, NULL, "pos", index2value(pos_values, pindex), NULL);
    if(gindex)
      string2xmlNode(gramGrpNode, NULL, "gen", index2value(gen_values, gindex), NULL);
    if(nindex)
      string2xmlNode(gramGrpNode, NULL, "num", index2value(num_values, nindex), NULL);
    xmlNodeAddContent(gramGrpNode, "\n  "); 
  }
  
  // sense
  int i;
  for(i=0; i < senses->len; i++)
  {
    xmlNodeAddContent(entryNode, "  "); 
    Sense *s = &g_array_index(senses, Sense, i);
    xmlNodePtr senseNode = xmlNewChild(entryNode, NULL, "sense", "\n");

    // domain
    gint dindex = gtk_option_menu_get_history(GTK_OPTION_MENU(s->domain_optionmenu));
    // if anything other than 'None' was selected in optionmenu
    if(dindex)
    {
      xmlNodePtr usgNode = string2xmlNode(senseNode, "    ", "usg",
	  index2value(domain_values, dindex), "\n");
      xmlNewProp(usgNode, "type", "dom");
    }

    // trans
    int j;
    for(j=0; j < s->trans->len; j++)
    {
      Sense_trans *t = &g_array_index(s->trans, Sense_trans, j);
      // tr
      if(strlen(gtk_entry_get_text(GTK_ENTRY(t->entry))))
      {
        xmlNodePtr transNode = string2xmlNode(senseNode, "    ", "trans",
	    NULL, "\n");
        GtkEntry2xmlNode(transNode, NULL, "tr", GTK_ENTRY(t->entry), NULL);
	// pos
        gint index = gtk_option_menu_get_history(GTK_OPTION_MENU(t->pos_optionmenu));
	// if anything other than 'None' was selected in optionmenu
	if(index>0)
	  string2xmlNode(transNode, NULL, "pos", index2value(pos_values, index), NULL);
	// gen
        index = gtk_option_menu_get_history(GTK_OPTION_MENU(t->gen_optionmenu));
	if(index>0)
	  string2xmlNode(transNode, NULL, "gen", index2value(gen_values, index), NULL);
      }
    }
    
    // def
    GtkEntry2xmlNode(senseNode, "    ", "def", GTK_ENTRY(s->def_entry), "\n");
    
    // note
    GtkEntry2xmlNode(senseNode, "    ", "note", GTK_ENTRY(s->note_entry), "\n");
    
    // eg
    if(strlen(gtk_entry_get_text(GTK_ENTRY(s->example_entry))) ||
       strlen(gtk_entry_get_text(GTK_ENTRY(s->example_tr_entry))))
    {
      xmlNodePtr egNode = string2xmlNode(senseNode, "    ", "eg", NULL, "\n");
      GtkEntry2xmlNode(egNode, NULL, "q", GTK_ENTRY(s->example_entry), NULL);

      // translation of example, if available
      if(strlen(gtk_entry_get_text(GTK_ENTRY(s->example_tr_entry))))
      {
        xmlNodePtr egTransNode =
	  string2xmlNode(egNode, "      ", "trans", NULL, "\n");
	GtkEntry2xmlNode(egTransNode, NULL, "tr",
	    GTK_ENTRY(s->example_tr_entry), NULL);
      }
      
    }

    // xr 
    for(j=0; j < s->xr->len; j++)
    {
      Sense_xr *xr = &g_array_index(s->xr, Sense_xr, j);
      if(strlen(gtk_entry_get_text(GTK_ENTRY(xr->combo_entry))))
      {
        xmlNodePtr xrNode = string2xmlNode(senseNode, "    ", "xr", NULL, "\n");
	
	// @type
        gint index = gtk_option_menu_get_history(
	    GTK_OPTION_MENU(xr->type_optionmenu));
	// if anything other than 'None' was selected in optionmenu
	if(index)
	  xmlNewProp(xrNode, "type", index2value(xr_values, index));
	
	GtkEntry2xmlNode(xrNode, NULL, "ref", GTK_ENTRY(xr->combo_entry), NULL);
      } // if
    } // xr
    xmlNodeAddContent(senseNode, "  "); 
  } // sense
  xmlNodeAddContent(entryNode, "\n"); 

  // XXX add <note resp='translator>Name <Email>  Date</note>

  return entryNode;
}
