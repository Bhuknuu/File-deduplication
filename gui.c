#include "gui.h"
#include "traversal.h"
#include "filter.h"
#include "report.h"
#include "action.h"
#include <gtk/gtk.h>
#include <windows.h>

// Global variables for GUI components
static GtkWidget *window;
static GtkWidget *directory_listbox;
static GtkWidget *add_dir_button;
static GtkWidget *remove_dir_button;
static GtkWidget *scan_button;
static GtkWidget *find_duplicates_button;
static GtkWidget *results_text;
static GtkWidget *progress_bar;
static GtkWidget *status_label;

// Action buttons
static GtkWidget *delete_first_button;
static GtkWidget *delete_selected_button;
static GtkWidget *hard_link_button;
static GtkWidget *soft_link_button;
static GtkWidget *move_to_folder_button;

// Global variables for file data
static FileInfo *files = NULL;
static int file_count = 0;
static DuplicateResults duplicates = {0};
static GList *directories = NULL;

// Forward declarations
static void display_results_in_textview(DuplicateResults* results);
static void enable_action_buttons(gboolean enabled);

// Thread data structure
typedef struct {
    char** dir_list;
    int dir_count;
} ScanThreadData;

// Helper function to scan multiple directories
static int scan_multiple_directories(char** directories, int dir_count, FileInfo* files, int max_files) {
    int total_count = 0;
    
    for (int i = 0; i < dir_count && total_count < max_files; i++) {
        int count = scan_directory(directories[i], files + total_count, max_files - total_count);
        total_count += count;
    }
    
    return total_count;
}

// Thread function for scanning directories
static DWORD WINAPI scan_directory_thread(LPVOID data) {
    ScanThreadData* thread_data = (ScanThreadData*)data;
    
    if (files != NULL) {
        free(files);
        files = NULL;
    }
    file_count = 0;
    free_duplicate_results(&duplicates);
    
    files = (FileInfo*)malloc(MAX_FILES * sizeof(FileInfo));
    if (files == NULL) {
        gchar *status_text = g_strdup("Error: Memory allocation failed");
        g_object_set_data_full(G_OBJECT(status_label), "text", status_text, g_free);
        free(thread_data->dir_list);
        free(thread_data);
        return 0;
    }
    
    file_count = scan_multiple_directories(thread_data->dir_list, thread_data->dir_count, files, MAX_FILES);
    
    // Clean up thread data
    for (int i = 0; i < thread_data->dir_count; i++) {
        free(thread_data->dir_list[i]);
    }
    free(thread_data->dir_list);
    free(thread_data);
    
    return 0;
}

// Idle callback to update UI after scanning
static gboolean update_scan_ui(gpointer data) {
    gchar *status_text;
    if (file_count > 0) {
        status_text = g_strdup_printf("Scan completed. Found %d files.", file_count);
        gtk_widget_set_sensitive(find_duplicates_button, TRUE);
    } else {
        status_text = g_strdup("No files found or error scanning directories.");
    }
    
    gtk_label_set_text(GTK_LABEL(status_label), status_text);
    g_free(status_text);
    
    gtk_widget_set_sensitive(scan_button, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
    
    return FALSE;
}

// Thread wrapper that updates UI
static DWORD WINAPI scan_with_ui_update(LPVOID data) {
    scan_directory_thread(data);
    g_idle_add(update_scan_ui, NULL);
    return 0;
}

// Idle callback to update UI after finding duplicates
static gboolean update_duplicates_ui(gpointer data) {
    display_results_in_textview(&duplicates);
    
    gchar *status_text;
    if (duplicates.count > 0) {
        status_text = g_strdup_printf("Found %d groups of duplicate files.", duplicates.count);
        enable_action_buttons(TRUE);
    } else {
        status_text = g_strdup("No duplicate files found.");
        enable_action_buttons(FALSE);
    }
    
    gtk_label_set_text(GTK_LABEL(status_label), status_text);
    g_free(status_text);
    
    gtk_widget_set_sensitive(scan_button, TRUE);
    gtk_widget_set_sensitive(find_duplicates_button, TRUE);
    
    return FALSE;
}

// Thread function for finding duplicates
static DWORD WINAPI find_duplicates_thread(LPVOID data) {
    free_duplicate_results(&duplicates);
    duplicates = find_duplicates(files, file_count);
    
    g_idle_add(update_duplicates_ui, NULL);
    
    return 0;
}

// Function to display results in the text view
static void display_results_in_textview(DuplicateResults* results) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(results_text));
    gtk_text_buffer_set_text(buffer, "", -1);
    
    if (results == NULL || results->count == 0) {
        gtk_text_buffer_insert_at_cursor(buffer, "No duplicate files to display.\n", -1);
        return;
    }
    
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(buffer, &iter);
    
    gchar *header = g_strdup_printf("\nDUPLICATE FILES REPORT\n"
                                   "======================\n"
                                   "Found %d groups of duplicate files.\n\n", 
                                   results->count);
    gtk_text_buffer_insert(buffer, &iter, header, -1);
    g_free(header);
    
    long long total_wasted = 0;
    
    for (int i = 0; i < results->count; i++) {
        DuplicateGroup* group = &results->groups[i];
        long long group_wasted = (long long)(group->count - 1) * group->files[0].size;
        total_wasted += group_wasted;
        
        gtk_text_buffer_get_end_iter(buffer, &iter);
        gchar *group_info = g_strdup_printf("Group %d: %d duplicates\n", i + 1, group->count);
        gtk_text_buffer_insert(buffer, &iter, group_info, -1);
        g_free(group_info);
        
        gtk_text_buffer_get_end_iter(buffer, &iter);
        gchar *size_info = g_strdup_printf("  Size: %lld bytes (%.2f MB)\n", 
                                          group->files[0].size,
                                          group->files[0].size / (1024.0 * 1024.0));
        gtk_text_buffer_insert(buffer, &iter, size_info, -1);
        g_free(size_info);
        
        gtk_text_buffer_get_end_iter(buffer, &iter);
        gchar *hash_info = g_strdup_printf("  Hash: %s\n", group->files[0].hash);
        gtk_text_buffer_insert(buffer, &iter, hash_info, -1);
        g_free(hash_info);
        
        gtk_text_buffer_get_end_iter(buffer, &iter);
        gchar *wasted_info = g_strdup_printf("  Wasted space: %lld bytes (%.2f MB)\n\n", 
                                             group_wasted,
                                             group_wasted / (1024.0 * 1024.0));
        gtk_text_buffer_insert(buffer, &iter, wasted_info, -1);
        g_free(wasted_info);
        
        gtk_text_buffer_get_end_iter(buffer, &iter);
        gtk_text_buffer_insert(buffer, &iter, "  Files:\n", -1);
        
        for (int j = 0; j < group->count; j++) {
            char time_buffer[100] = "Unknown";
            struct tm *timeinfo = localtime(&group->files[j].modified);
            if (timeinfo != NULL) {
                strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
            }
            
            gtk_text_buffer_get_end_iter(buffer, &iter);
            gchar *file_info = g_strdup_printf("    [%d] %s (Modified: %s)\n", 
                                              j + 1,
                                              group->files[j].path, 
                                              time_buffer);
            gtk_text_buffer_insert(buffer, &iter, file_info, -1);
            g_free(file_info);
        }
        
        gtk_text_buffer_get_end_iter(buffer, &iter);
        gtk_text_buffer_insert(buffer, &iter, "\n", -1);
    }
    
    gtk_text_buffer_get_end_iter(buffer, &iter);
    gchar *summary = g_strdup_printf("SUMMARY\n"
                                    "=======\n"
                                    "Total duplicate groups: %d\n"
                                    "Total wasted space: %lld bytes (%.2f MB)\n", 
                                    results->count, 
                                    total_wasted, 
                                    (double)total_wasted / (1024.0 * 1024.0));
    gtk_text_buffer_insert(buffer, &iter, summary, -1);
    g_free(summary);
}

// Enable/disable action buttons
static void enable_action_buttons(gboolean enabled) {
    gtk_widget_set_sensitive(delete_first_button, enabled);
    gtk_widget_set_sensitive(delete_selected_button, enabled);
    gtk_widget_set_sensitive(hard_link_button, enabled);
    gtk_widget_set_sensitive(soft_link_button, enabled);
    gtk_widget_set_sensitive(move_to_folder_button, enabled);
}

// Callback for add directory button
static void on_add_directory_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Directory",
                                                     GTK_WINDOW(window),
                                                     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Open", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new(folder);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_container_add(GTK_CONTAINER(row), label);
        gtk_widget_show_all(row);
        gtk_list_box_insert(GTK_LIST_BOX(directory_listbox), row, -1);
        
        directories = g_list_append(directories, g_strdup(folder));
        
        g_free(folder);
    }
    
    gtk_widget_destroy(dialog);
}

// Callback for remove directory button
static void on_remove_directory_clicked(GtkWidget *widget, gpointer data) {
    GtkListBoxRow *selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(directory_listbox));
    
    if (selected) {
        int index = gtk_list_box_row_get_index(selected);
        
        GList *item = g_list_nth(directories, index);
        if (item) {
            g_free(item->data);
            directories = g_list_delete_link(directories, item);
        }
        
        gtk_widget_destroy(GTK_WIDGET(selected));
    }
}

// Callback for scan button
static void on_scan_button_clicked(GtkWidget *widget, gpointer data) {
    if (g_list_length(directories) == 0) {
        gtk_label_set_text(GTK_LABEL(status_label), "Please add at least one directory.");
        return;
    }
    
    gtk_widget_set_sensitive(scan_button, FALSE);
    enable_action_buttons(FALSE);
    gtk_label_set_text(GTK_LABEL(status_label), "Scanning directories...");
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress_bar));
    
    ScanThreadData *thread_data = malloc(sizeof(ScanThreadData));
    thread_data->dir_count = g_list_length(directories);
    thread_data->dir_list = malloc(thread_data->dir_count * sizeof(char*));
    
    GList *current = directories;
    for (int i = 0; i < thread_data->dir_count; i++) {
        thread_data->dir_list[i] = _strdup((char*)current->data);
        current = current->next;
    }
    
    HANDLE thread = CreateThread(NULL, 0, scan_with_ui_update, thread_data, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    }
}

// Callback for find duplicates button
static void on_find_duplicates_button_clicked(GtkWidget *widget, gpointer data) {
    if (file_count == 0) {
        gtk_label_set_text(GTK_LABEL(status_label), "Please scan directories first!");
        return;
    }
    
    gtk_widget_set_sensitive(scan_button, FALSE);
    gtk_widget_set_sensitive(find_duplicates_button, FALSE);
    enable_action_buttons(FALSE);
    gtk_label_set_text(GTK_LABEL(status_label), "Finding duplicates...");
    
    HANDLE thread = CreateThread(NULL, 0, find_duplicates_thread, NULL, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    }
}

// Callback for delete all except first
static void on_delete_first_clicked(GtkWidget *widget, gpointer data) {
    int total_files = 0;
    for (int i = 0; i < duplicates.count; i++) {
        total_files += duplicates.groups[i].count - 1;
    }
    
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_WARNING,
                                               GTK_BUTTONS_YES_NO,
                                               "Delete all duplicates except the first file in each group?");
    
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "This will permanently delete %d files!", total_files);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        int removed = remove_duplicates_except_first(&duplicates);
        gchar *msg = g_strdup_printf("Deleted %d duplicate files.", removed);
        gtk_label_set_text(GTK_LABEL(status_label), msg);
        g_free(msg);
        
        enable_action_buttons(FALSE);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(results_text)), 
                                "Files deleted. Scan again to update.", -1);
    }
    
    gtk_widget_destroy(dialog);
}

// Callback for delete selected indices
static void on_delete_selected_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Select Index to Keep",
                                                     GTK_WINDOW(window),
                                                     GTK_DIALOG_MODAL,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_OK", GTK_RESPONSE_OK,
                                                     NULL);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new("Enter the file index to keep (1, 2, 3, etc.):");
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), "1");
    
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 5);
    gtk_widget_show_all(content);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        int index = atoi(text) - 1;
        
        if (index >= 0) {
            int removed = remove_duplicates_except_index(&duplicates, index);
            gchar *msg = g_strdup_printf("Deleted %d duplicate files.", removed);
            gtk_label_set_text(GTK_LABEL(status_label), msg);
            g_free(msg);
            
            enable_action_buttons(FALSE);
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(results_text)), 
                                    "Files deleted. Scan again to update.", -1);
        }
    }
    
    gtk_widget_destroy(dialog);
}

// Callback for hard link
static void on_hard_link_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Create hard links? Keep first file, link others?");
    
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "This will replace duplicates with hard links.");
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        int linked = create_hard_links(&duplicates, 0);
        gchar *msg = g_strdup_printf("Created %d hard links.", linked);
        gtk_label_set_text(GTK_LABEL(status_label), msg);
        g_free(msg);
    }
    
    gtk_widget_destroy(dialog);
}

// Callback for symbolic link
static void on_soft_link_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "Create symbolic links? Keep first file, link others?");
    
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "This will replace duplicates with symbolic links.");
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        int linked = create_symbolic_links(&duplicates, 0);
        gchar *msg = g_strdup_printf("Created %d symbolic links.", linked);
        gtk_label_set_text(GTK_LABEL(status_label), msg);
        g_free(msg);
    }
    
    gtk_widget_destroy(dialog);
}

// Callback for move to folder
static void on_move_to_folder_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Destination Folder",
                                                     GTK_WINDOW(window),
                                                     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Select", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        int moved = move_duplicates_to_folder(&duplicates, 0, folder);
        gchar *msg = g_strdup_printf("Moved %d duplicate files to %s", moved, folder);
        gtk_label_set_text(GTK_LABEL(status_label), msg);
        g_free(msg);
        
        g_free(folder);
    }
    
    gtk_widget_destroy(dialog);
}

// Callback for window close
static gboolean on_window_close(GtkWidget *widget, GdkEvent *event, gpointer data) {
    if (files != NULL) {
        free(files);
    }
    free_duplicate_results(&duplicates);
    g_list_free_full(directories, g_free);
    
    gtk_main_quit();
    return FALSE;
}

// Function to initialize the GUI
int run_gui(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "File Deduplication System");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 700);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_window_close), NULL);
    
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), main_vbox);
    
    // Title
    GtkWidget *title_label = gtk_label_new("FILE DEDUPLICATION SYSTEM");
    gtk_widget_set_name(title_label, "title-label");
    gtk_box_pack_start(GTK_BOX(main_vbox), title_label, FALSE, FALSE, 5);
    
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider,
        "#title-label { font-size: 16pt; font-weight: bold; color: #1e83d1ff; }", -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(title_label);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(css_provider);
    
    // Directory selection section
    GtkWidget *dir_frame = gtk_frame_new("Directories to Scan");
    gtk_box_pack_start(GTK_BOX(main_vbox), dir_frame, FALSE, FALSE, 5);
    
    GtkWidget *dir_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(dir_vbox), 5);
    gtk_container_add(GTK_CONTAINER(dir_frame), dir_vbox);
    
    GtkWidget *dir_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(dir_scroll), 
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(dir_scroll, -1, 100);
    gtk_box_pack_start(GTK_BOX(dir_vbox), dir_scroll, TRUE, TRUE, 0);
    
    directory_listbox = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(dir_scroll), directory_listbox);
    
    GtkWidget *dir_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(dir_vbox), dir_button_box, FALSE, FALSE, 0);
    
    add_dir_button = gtk_button_new_with_label("Add Directory");
    g_signal_connect(add_dir_button, "clicked", G_CALLBACK(on_add_directory_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(dir_button_box), add_dir_button, TRUE, TRUE, 0);
    
    remove_dir_button = gtk_button_new_with_label("Remove Selected");
    g_signal_connect(remove_dir_button, "clicked", G_CALLBACK(on_remove_directory_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(dir_button_box), remove_dir_button, TRUE, TRUE, 0);
    
    // Scan buttons
    GtkWidget *scan_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_vbox), scan_box, FALSE, FALSE, 5);
    
    scan_button = gtk_button_new_with_label("Scan All Directories");
    g_signal_connect(scan_button, "clicked", G_CALLBACK(on_scan_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(scan_box), scan_button, TRUE, TRUE, 0);
    
    find_duplicates_button = gtk_button_new_with_label("Find Duplicates");
    g_signal_connect(find_duplicates_button, "clicked", G_CALLBACK(on_find_duplicates_button_clicked), NULL);
    gtk_widget_set_sensitive(find_duplicates_button, FALSE);
    gtk_box_pack_start(GTK_BOX(scan_box), find_duplicates_button, TRUE, TRUE, 0);
    
    // Progress bar
    progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(main_vbox), progress_bar, FALSE, FALSE, 5);
    
    // Status label
    status_label = gtk_label_new("Ready - Add directories and click Scan");
    gtk_box_pack_start(GTK_BOX(main_vbox), status_label, FALSE, FALSE, 5);
    
    // Action buttons frame
    GtkWidget *action_frame = gtk_frame_new("Actions");
    gtk_box_pack_start(GTK_BOX(main_vbox), action_frame, FALSE, FALSE, 5);
    
    GtkWidget *action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(action_box), 5);
    gtk_container_add(GTK_CONTAINER(action_frame), action_box);
    
    delete_first_button = gtk_button_new_with_label("Delete (Keep First)");
    g_signal_connect(delete_first_button, "clicked", G_CALLBACK(on_delete_first_clicked), NULL);
    gtk_widget_set_sensitive(delete_first_button, FALSE);
    gtk_box_pack_start(GTK_BOX(action_box), delete_first_button, TRUE, TRUE, 0);
    
    delete_selected_button = gtk_button_new_with_label("Delete (Keep Index)");
    g_signal_connect(delete_selected_button, "clicked", G_CALLBACK(on_delete_selected_clicked), NULL);
    gtk_widget_set_sensitive(delete_selected_button, FALSE);
    gtk_box_pack_start(GTK_BOX(action_box), delete_selected_button, TRUE, TRUE, 0);
    
    hard_link_button = gtk_button_new_with_label("Hard Link");
    g_signal_connect(hard_link_button, "clicked", G_CALLBACK(on_hard_link_clicked), NULL);
    gtk_widget_set_sensitive(hard_link_button, FALSE);
    gtk_box_pack_start(GTK_BOX(action_box), hard_link_button, TRUE, TRUE, 0);
    
    soft_link_button = gtk_button_new_with_label("Symbolic Link");
    g_signal_connect(soft_link_button, "clicked", G_CALLBACK(on_soft_link_clicked), NULL);
    gtk_widget_set_sensitive(soft_link_button, FALSE);
    gtk_box_pack_start(GTK_BOX(action_box), soft_link_button, TRUE, TRUE, 0);
    
    move_to_folder_button = gtk_button_new_with_label("Move to Folder");
    g_signal_connect(move_to_folder_button, "clicked", G_CALLBACK(on_move_to_folder_clicked), NULL);
    gtk_widget_set_sensitive(move_to_folder_button, FALSE);
    gtk_box_pack_start(GTK_BOX(action_box), move_to_folder_button, TRUE, TRUE, 0);
    
    // Results text view
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), 
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(main_vbox), scrolled_window, TRUE, TRUE, 5);
    
    results_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(results_text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(results_text), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scrolled_window), results_text);
    
    gtk_widget_show_all(window);
    gtk_main();
    
    return 0;
}