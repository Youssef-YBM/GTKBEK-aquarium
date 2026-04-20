#include "structures.h"
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_FISH 200
#define MAX_GROUPS 20
#define WINDOW_WIDTH 1366
#define WINDOW_HEIGHT 768
#define TIMEOUT_MS 33
#define AVOID_DISTANCE 150
#define REGROUP_DELAY 2000
#define MAX_SPEED_MULTIPLIER 4.0

typedef struct {
    double x, y;
    double vx, vy;
    int group_id;
    int is_leader;
    int is_predator;
    int aggression_level;
    int width, height;
    double angle;
    int fleeing;
    guint flee_timeout_id;
    int active;
} Fish;

typedef struct {
    int group_id;
    char *image_path;
    char *group_name;
    double speed;
    int member_count;
    int members_indices[MAX_FISH];
    GdkPixbuf *pixbuf_cache;
    cairo_surface_t *surface;
    int img_width, img_height;
    int leader_index;
    int is_predator_group;
} FishGroup;

// Global variables
Fish fishes[MAX_FISH];
FishGroup groups[MAX_GROUPS];
int fish_count = 0;
int group_count = 0;
GtkWidget *drawing_area;
window *main_window;
box *sidebar;
box *main_hbox;
box *groups_list_box;
int animation_timeout_id = 0;
int hover_active = 0;
double hover_x = 0, hover_y = 0;
int is_fullscreen = 0;
GdkPixbuf *background_pixbuf = NULL;
cairo_surface_t *background_surface = NULL;
int current_width = WINDOW_WIDTH;
int current_height = WINDOW_HEIGHT;

// Function prototypes
void add_fish(int group_id);
void create_fish_group(const char *image_path, const char *group_name, double speed, int is_predator_group);
void update_animation();
gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean on_mouse_move(GtkWidget *widget, GdkEventMotion *event, gpointer data);
gboolean animation_tick(gpointer data);
gboolean regroup_fish(gpointer data);
void flee_from_hover(int fish_idx);
void delete_fish(int fish_idx);
void load_background();
void toggle_fullscreen();
void draw_bubbles(cairo_t *cr, int width, int height);
void refresh_groups_list();
void on_add_normal_fish(GtkWidget *widget, gpointer data);
void on_add_predator_group(GtkWidget *widget, gpointer data);
void on_add_fish_group(GtkWidget *widget, gpointer data);
void on_delete_fish(GtkWidget *widget, gpointer data);
void on_window_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data);

// Create beautiful underwater background
void load_background() {
    if (background_surface) {
        cairo_surface_destroy(background_surface);
    }
    
    background_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, current_width, current_height);
    cairo_t *cr = cairo_create(background_surface);
    
    // Create vertical gradient
    cairo_pattern_t *gradient = cairo_pattern_create_linear(0, 0, 0, current_height);
    cairo_pattern_add_color_stop_rgb(gradient, 0.0, 0.1, 0.3, 0.6);
    cairo_pattern_add_color_stop_rgb(gradient, 0.4, 0.05, 0.2, 0.5);
    cairo_pattern_add_color_stop_rgb(gradient, 0.7, 0.02, 0.1, 0.4);
    cairo_pattern_add_color_stop_rgb(gradient, 1.0, 0.0, 0.05, 0.3);
    
    cairo_rectangle(cr, 0, 0, current_width, current_height);
    cairo_set_source(cr, gradient);
    cairo_fill(cr);
    cairo_pattern_destroy(gradient);
    
    // Add light rays
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.9, 0.08);
    for (int i = 0; i < 30; i++) {
        double x = (rand() % current_width);
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x + (rand() % 300) - 150, current_height);
        cairo_line_to(cr, x + (rand() % 150) - 75, current_height);
        cairo_fill(cr);
    }
    
    cairo_destroy(cr);
}

void draw_bubbles(cairo_t *cr, int width, int height) {
    static int bubble_frame = 0;
    bubble_frame = (bubble_frame + 1) % 360;
    
    for (int i = 0; i < 40; i++) {
        int x = (i * 173 + bubble_frame * 2) % width;
        int y = (height - (i * 37 + bubble_frame * 3) % height);
        int size = 2 + (i % 8);
        
        cairo_set_source_rgba(cr, 0.7, 0.8, 1.0, 0.4);
        cairo_arc(cr, x, y, size, 0, 2 * M_PI);
        cairo_fill(cr);
        
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.6);
        cairo_arc(cr, x - size/3, y - size/3, size/4, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}

cairo_surface_t* load_fish_surface(const char *path, int *out_width, int *out_height, int target_size) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);
    
    if (!pixbuf) {
        g_printerr("ERROR: Failed to load image %s: %s\n", path, error->message);
        g_error_free(error);
        
        // Create a fallback colored circle with a question mark
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_size, target_size);
        cairo_t *cr = cairo_create(surface);
        
        // Draw circle
        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_arc(cr, target_size/2, target_size/2, target_size/2, 0, 2 * M_PI);
        cairo_fill(cr);
        
        // Draw question mark
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, target_size * 0.6);
        cairo_move_to(cr, target_size * 0.35, target_size * 0.7);
        cairo_show_text(cr, "?");
        
        cairo_destroy(cr);
        *out_width = target_size;
        *out_height = target_size;
        return surface;
    }
    
    int orig_width = gdk_pixbuf_get_width(pixbuf);
    int orig_height = gdk_pixbuf_get_height(pixbuf);
    double ratio = (double)orig_width / orig_height;
    
    *out_width = target_size;
    *out_height = (int)(target_size / ratio);
    if (*out_height > target_size) {
        *out_height = target_size;
        *out_width = (int)(target_size * ratio);
    }
    
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, *out_width, *out_height, GDK_INTERP_BILINEAR);
    cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf(scaled, 1.0, NULL);
    
    g_object_unref(pixbuf);
    g_object_unref(scaled);
    
    return surface;
}

void create_fish_group(const char *image_path, const char *group_name, double speed, int is_predator_group) {
    if (group_count >= MAX_GROUPS) {
        g_printerr("Maximum number of groups reached!\n");
        return;
    }
    
    groups[group_count].group_id = group_count;
    groups[group_count].image_path = g_strdup(image_path);
    groups[group_count].group_name = g_strdup(group_name);
    groups[group_count].speed = speed;
    groups[group_count].member_count = 0;
    groups[group_count].leader_index = -1;
    groups[group_count].is_predator_group = is_predator_group;
    
    // Load surface for drawing
    groups[group_count].surface = load_fish_surface(image_path, 
                                                     &groups[group_count].img_width,
                                                     &groups[group_count].img_height, 50);
    
    group_count++;
    refresh_groups_list();
}

void add_fish(int group_id) {
    if (fish_count >= MAX_FISH) {
        g_printerr("Maximum number of fish reached!\n");
        return;
    }
    if (group_id >= group_count) {
        g_printerr("Invalid group ID!\n");
        return;
    }
    
    Fish *f = &fishes[fish_count];
    f->group_id = group_id;
    f->is_predator = groups[group_id].is_predator_group;
    f->aggression_level = f->is_predator ? 5 : 1;
    f->is_leader = 0;
    f->fleeing = 0;
    f->flee_timeout_id = 0;
    f->active = 1;
    f->width = groups[group_id].img_width;
    f->height = groups[group_id].img_height;
    f->angle = 0;
    
    // Random position
    f->x = (rand() % (current_width - 200)) + 100;
    f->y = (rand() % (current_height - 200)) + 100;
    
    // Random direction
    double angle = (rand() % 360) * M_PI / 180.0;
    f->vx = cos(angle) * groups[group_id].speed;
    f->vy = sin(angle) * groups[group_id].speed;
    
    // Add to group members list
    groups[group_id].members_indices[groups[group_id].member_count] = fish_count;
    f->is_leader = (groups[group_id].member_count == 0);
    if (f->is_leader) {
        groups[group_id].leader_index = groups[group_id].member_count;
    }
    
    fish_count++;
    groups[group_id].member_count++;
}

void delete_fish(int fish_idx) {
    if (fish_idx < 0 || fish_idx >= fish_count) return;
    
    Fish *f = &fishes[fish_idx];
    if (f->group_id >= group_count) return;
    
    FishGroup *g = &groups[f->group_id];
    
    // Find position in group
    int group_pos = -1;
    for (int i = 0; i < g->member_count; i++) {
        if (g->members_indices[i] == fish_idx) {
            group_pos = i;
            break;
        }
    }
    
    if (group_pos >= 0) {
        // Remove from group
        for (int i = group_pos; i < g->member_count - 1; i++) {
            g->members_indices[i] = g->members_indices[i + 1];
        }
        g->member_count--;
        
        // Update leader if needed
        if (group_pos == g->leader_index && g->member_count > 0) {
            g->leader_index = 0;
            int new_leader_idx = g->members_indices[0];
            if (new_leader_idx >= 0 && new_leader_idx < fish_count) {
                fishes[new_leader_idx].is_leader = 1;
            }
        } else if (g->leader_index > group_pos) {
            g->leader_index--;
        }
    }
    
    // Cancel any pending timeout
    if (f->flee_timeout_id) {
        g_source_remove(f->flee_timeout_id);
    }
    
    // Shift remaining fish
    for (int i = fish_idx; i < fish_count - 1; i++) {
        fishes[i] = fishes[i + 1];
    }
    fish_count--;
    
    // Update indices in all groups
    for (int gid = 0; gid < group_count; gid++) {
        for (int i = 0; i < groups[gid].member_count; i++) {
            if (groups[gid].members_indices[i] > fish_idx) {
                groups[gid].members_indices[i]--;
            }
        }
    }
}

void update_fish_movement() {
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (f->group_id >= group_count) continue;
        
        FishGroup *g = &groups[f->group_id];
        
        if (f->fleeing) {
            double dx = f->x - hover_x;
            double dy = f->y - hover_y;
            double dist = sqrt(dx*dx + dy*dy);
            if (dist > 0.01 && dist < AVOID_DISTANCE * 2) {
                double angle = atan2(dy, dx);
                f->vx = cos(angle) * g->speed * MAX_SPEED_MULTIPLIER;
                f->vy = sin(angle) * g->speed * MAX_SPEED_MULTIPLIER;
            }
        } else if (!f->is_leader && g->leader_index >= 0 && g->leader_index < g->member_count) {
            int leader_idx = g->members_indices[g->leader_index];
            if (leader_idx >= 0 && leader_idx < fish_count && fishes[leader_idx].active) {
                Fish *leader = &fishes[leader_idx];
                double dx = leader->x - f->x;
                double dy = leader->y - f->y;
                double dist = sqrt(dx*dx + dy*dy);
                if (dist > 40) {
                    double angle = atan2(dy, dx);
                    double target_vx = cos(angle) * g->speed;
                    double target_vy = sin(angle) * g->speed;
                    f->vx = f->vx * 0.92 + target_vx * 0.08;
                    f->vy = f->vy * 0.92 + target_vy * 0.08;
                } else {
                    f->vx += ((rand() % 100) - 50) / 500.0;
                    f->vy += ((rand() % 100) - 50) / 500.0;
                    double len = sqrt(f->vx*f->vx + f->vy*f->vy);
                    if (len > g->speed) {
                        f->vx = (f->vx / len) * g->speed;
                        f->vy = (f->vy / len) * g->speed;
                    }
                }
            }
        } else if (f->is_leader) {
            if (rand() % 100 < 3) {
                double angle = (rand() % 360) * M_PI / 180.0;
                f->vx = cos(angle) * g->speed;
                f->vy = sin(angle) * g->speed;
            }
            f->vx += ((rand() % 100) - 50) / 1000.0;
            f->vy += ((rand() % 100) - 50) / 1000.0;
        }
        
        f->x += f->vx;
        f->y += f->vy;
        
        int padding = 100;
        if (f->x < -padding) f->x = current_width + padding;
        if (f->x > current_width + padding) f->x = -padding;
        if (f->y < -padding) f->y = current_height + padding;
        if (f->y > current_height + padding) f->y = -padding;
        
        double len = sqrt(f->vx*f->vx + f->vy*f->vy);
        double max_speed = f->fleeing ? g->speed * MAX_SPEED_MULTIPLIER : g->speed * 1.2;
        if (len > max_speed && max_speed > 0) {
            f->vx = (f->vx / len) * max_speed;
            f->vy = (f->vy / len) * max_speed;
        }
        
        f->angle = atan2(f->vy, f->vx);
    }
}

void flee_from_hover(int fish_idx) {
    if (fish_idx < 0 || fish_idx >= fish_count) return;
    
    Fish *trigger = &fishes[fish_idx];
    if (!trigger->active) return;
    
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (!f->active) continue;
        
        int should_flee = 0;
        if (trigger->is_predator && !f->is_predator) {
            should_flee = 1;
        } else if (!f->is_predator && trigger->aggression_level > f->aggression_level) {
            should_flee = 1;
        }
        
        if (should_flee && !f->fleeing) {
            f->fleeing = 1;
            
            if (f->flee_timeout_id) {
                g_source_remove(f->flee_timeout_id);
            }
            int *idx_ptr = malloc(sizeof(int));
            *idx_ptr = i;
            f->flee_timeout_id = g_timeout_add(REGROUP_DELAY, regroup_fish, idx_ptr);
        }
    }
}

gboolean regroup_fish(gpointer data) {
    int *fish_idx_ptr = (int*)data;
    int fish_idx = *fish_idx_ptr;
    free(data);
    
    if (fish_idx >= 0 && fish_idx < fish_count) {
        fishes[fish_idx].fleeing = 0;
        fishes[fish_idx].flee_timeout_id = 0;
    }
    return FALSE;
}

gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    
    if (allocation.width != current_width || allocation.height != current_height) {
        current_width = allocation.width;
        current_height = allocation.height;
        load_background();
    }
    
    if (background_surface) {
        cairo_set_source_surface(cr, background_surface, 0, 0);
        cairo_paint(cr);
    } else {
        cairo_set_source_rgb(cr, 0.1, 0.2, 0.3);
        cairo_paint(cr);
    }
    
    draw_bubbles(cr, current_width, current_height);
    
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (!f->active) continue;
        if (f->group_id >= group_count) continue;
        
        FishGroup *g = &groups[f->group_id];
        if (!g->surface) continue;
        
        cairo_save(cr);
        cairo_translate(cr, f->x, f->y);
        cairo_rotate(cr, f->angle);
        
        if (f->vx < 0) {
            cairo_scale(cr, -1, 1);
        }
        
        cairo_set_source_surface(cr, g->surface, -f->width/2, -f->height/2);
        cairo_paint(cr);
        cairo_restore(cr);
        
        if (f->is_leader && !f->fleeing) {
            cairo_save(cr);
            cairo_set_source_rgb(cr, 1.0, 0.85, 0.2);
            cairo_arc(cr, f->x, f->y - f->height/2 - 5, 6, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        
        if (f->is_predator) {
            cairo_save(cr);
            cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 0.3);
            cairo_arc(cr, f->x, f->y, f->width/2 + 10, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        
        if (f->fleeing) {
            cairo_save(cr);
            cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
            cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 16);
            cairo_move_to(cr, f->x + 15, f->y - 15);
            cairo_show_text(cr, "!!");
            cairo_restore(cr);
        }
    }
    
    return FALSE;
}

gboolean on_mouse_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    hover_x = event->x;
    hover_y = event->y;
    hover_active = 1;
    
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (!f->active) continue;
        
        double dx = f->x - hover_x;
        double dy = f->y - hover_y;
        double dist = sqrt(dx*dx + dy*dy);
        if (dist < 40) {
            flee_from_hover(i);
            break;
        }
    }
    
    return FALSE;
}

gboolean animation_tick(gpointer data) {
    update_fish_movement();
    if (drawing_area) {
        gtk_widget_queue_draw(drawing_area);
    }
    return TRUE;
}

void toggle_fullscreen() {
    is_fullscreen = !is_fullscreen;
    if (is_fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(main_window->win));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(main_window->win));
    }
}

void refresh_groups_list() {
    // Clear existing group list
    if (groups_list_box) {
        clear_box(groups_list_box);
    }
    
    // Add each group as a button
    for (int i = 0; i < group_count; i++) {
        char button_text[256];
        const char *icon = groups[i].is_predator_group ? "🦈" : "🐟";
        snprintf(button_text, sizeof(button_text), "%s %s (Speed: %.1f)", 
                 icon, groups[i].group_name, groups[i].speed);
        
        button *group_btn = create_button();
        group_btn->label = g_strdup(button_text);
        group_btn->bg_color = groups[i].is_predator_group ? g_strdup("#e74c3c") : g_strdup("#2ecc71");
        group_btn->click_callback = on_add_normal_fish;
        group_btn->callback_data = GINT_TO_POINTER(i);
        update_button(group_btn);
        add_to_box(groups_list_box, group_btn->button);
    }
}

// Callbacks
void on_add_normal_fish(GtkWidget *widget, gpointer data) {
    int group_id = GPOINTER_TO_INT(data);
    if (group_id >= 0 && group_id < group_count) {
        add_fish(group_id);
    }
}

void on_add_predator_group(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Predator Image",
                                                     GTK_WINDOW(main_window->win),
                                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Open", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Image Files");
    gtk_file_filter_add_pixbuf_formats(filter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        // Dialog for group name
        GtkWidget *name_dialog = gtk_dialog_new_with_buttons("Predator Group Name",
                                                              GTK_WINDOW(main_window->win),
                                                              GTK_DIALOG_MODAL,
                                                              "_OK", GTK_RESPONSE_OK,
                                                              NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(name_dialog));
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter predator group name");
        
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Group Name:"), FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 5);
        gtk_container_add(GTK_CONTAINER(content), vbox);
        gtk_widget_show_all(content);
        
        if (gtk_dialog_run(GTK_DIALOG(name_dialog)) == GTK_RESPONSE_OK) {
            const char *group_name = gtk_entry_get_text(GTK_ENTRY(entry));
            if (strlen(group_name) == 0) group_name = "Predator";
            
            // Dialog for speed
            GtkWidget *speed_dialog = gtk_dialog_new_with_buttons("Set Predator Speed",
                                                                  GTK_WINDOW(main_window->win),
                                                                  GTK_DIALOG_MODAL,
                                                                  "_OK", GTK_RESPONSE_OK,
                                                                  NULL);
            GtkWidget *speed_content = gtk_dialog_get_content_area(GTK_DIALOG(speed_dialog));
            GtkWidget *spin = gtk_spin_button_new_with_range(1.0, 8.0, 0.5);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 2.5);
            
            GtkWidget *speed_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
            gtk_box_pack_start(GTK_BOX(speed_vbox), gtk_label_new("Predator Speed:"), FALSE, FALSE, 5);
            gtk_box_pack_start(GTK_BOX(speed_vbox), spin, FALSE, FALSE, 5);
            gtk_container_add(GTK_CONTAINER(speed_content), speed_vbox);
            gtk_widget_show_all(speed_content);
            
            if (gtk_dialog_run(GTK_DIALOG(speed_dialog)) == GTK_RESPONSE_OK) {
                double speed = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
                create_fish_group(filename, group_name, speed, 1);
            }
            gtk_widget_destroy(speed_dialog);
        }
        gtk_widget_destroy(name_dialog);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

void on_add_fish_group(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Fish Image",
                                                     GTK_WINDOW(main_window->win),
                                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Open", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Image Files");
    gtk_file_filter_add_pixbuf_formats(filter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        // Dialog for group name
        GtkWidget *name_dialog = gtk_dialog_new_with_buttons("Fish Group Name",
                                                              GTK_WINDOW(main_window->win),
                                                              GTK_DIALOG_MODAL,
                                                              "_OK", GTK_RESPONSE_OK,
                                                              NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(name_dialog));
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter fish group name");
        
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Group Name:"), FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 5);
        gtk_container_add(GTK_CONTAINER(content), vbox);
        gtk_widget_show_all(content);
        
        if (gtk_dialog_run(GTK_DIALOG(name_dialog)) == GTK_RESPONSE_OK) {
            const char *group_name = gtk_entry_get_text(GTK_ENTRY(entry));
            if (strlen(group_name) == 0) group_name = "Fish";
            
            // Dialog for speed
            GtkWidget *speed_dialog = gtk_dialog_new_with_buttons("Set Fish Speed",
                                                                  GTK_WINDOW(main_window->win),
                                                                  GTK_DIALOG_MODAL,
                                                                  "_OK", GTK_RESPONSE_OK,
                                                                  NULL);
            GtkWidget *speed_content = gtk_dialog_get_content_area(GTK_DIALOG(speed_dialog));
            GtkWidget *spin = gtk_spin_button_new_with_range(0.5, 6.0, 0.5);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 2.0);
            
            GtkWidget *speed_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
            gtk_box_pack_start(GTK_BOX(speed_vbox), gtk_label_new("Fish Speed:"), FALSE, FALSE, 5);
            gtk_box_pack_start(GTK_BOX(speed_vbox), spin, FALSE, FALSE, 5);
            gtk_container_add(GTK_CONTAINER(speed_content), speed_vbox);
            gtk_widget_show_all(speed_content);
            
            if (gtk_dialog_run(GTK_DIALOG(speed_dialog)) == GTK_RESPONSE_OK) {
                double speed = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
                create_fish_group(filename, group_name, speed, 0);
            }
            gtk_widget_destroy(speed_dialog);
        }
        gtk_widget_destroy(name_dialog);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

void on_delete_fish(GtkWidget *widget, gpointer data) {
    if (fish_count > 0) {
        delete_fish(fish_count - 1);
    }
}

void on_window_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    current_width = event->width;
    current_height = event->height;
    load_background();
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    
    ProgramStart();
    
    // Create main window
    main_window = create_window();
    main_window->title = "🐠 Aquarium Simulator - Create Your Own Fish!";
    main_window->width = WINDOW_WIDTH;
    main_window->height = WINDOW_HEIGHT;
    main_window->resizable = TRUE;
    main_window->bg_color = g_strdup("#0a1a2a");
    update_window(main_window);
    
    g_signal_connect(main_window->win, "configure-event", G_CALLBACK(on_window_configure), NULL);
    
    // Create main horizontal box
    main_hbox = create_hbox(0, FALSE);
    
    // Create sidebar
    sidebar = create_vbox(10, FALSE);
    sidebar->width = 260;
    sidebar->bg_color = g_strdup("rgba(0, 0, 0, 0.85)");
    sidebar->margin_top = sidebar->margin_bottom = sidebar->margin_left = sidebar->margin_right = 10;
    update_box(sidebar);
    
    // Title
    label *title_label = create_label();
    title_label->text = "🐠 AQUARIUM STUDIO";
    title_label->color = g_strdup("#FFD700");
    title_label->style = g_strdup("bold");
    title_label->font_family = g_strdup("Arial Black");
    update_label(title_label);
    add_to_box(sidebar, title_label->label);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    add_to_box(sidebar, sep);
    
    // Create group buttons
    button *create_fish_btn = create_button();
    create_fish_btn->label = "🐟 Create New Fish Group";
    create_fish_btn->bg_color = g_strdup("#2ecc71");
    create_fish_btn->click_callback = on_add_fish_group;
    update_button(create_fish_btn);
    add_to_box(sidebar, create_fish_btn->button);
    
    button *create_predator_btn = create_button();
    create_predator_btn->label = "🦈 Create New Predator Group";
    create_predator_btn->bg_color = g_strdup("#e74c3c");
    create_predator_btn->click_callback = on_add_predator_group;
    update_button(create_predator_btn);
    add_to_box(sidebar, create_predator_btn->button);
    
    button *delete_btn = create_button();
    delete_btn->label = "🗑️ Delete Last Fish";
    delete_btn->bg_color = g_strdup("#e67e22");
    delete_btn->click_callback = on_delete_fish;
    update_button(delete_btn);
    add_to_box(sidebar, delete_btn->button);
    
    button *fullscreen_btn = create_button();
    fullscreen_btn->label = "🖥️ Toggle Fullscreen";
    fullscreen_btn->bg_color = g_strdup("#9b59b6");
    fullscreen_btn->click_callback = toggle_fullscreen;
    update_button(fullscreen_btn);
    add_to_box(sidebar, fullscreen_btn->button);
    
    // Groups section title
    label *groups_title = create_label();
    groups_title->text = "📋 YOUR FISH GROUPS:";
    groups_title->color = g_strdup("#87CEEB");
    groups_title->style = g_strdup("bold");
    update_label(groups_title);
    add_to_box(sidebar, groups_title->label);
    
    // Scrollable area for groups list
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 300);
    
    groups_list_box = create_vbox(5, FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled), get_box_widget(groups_list_box));
    add_to_box(sidebar, scrolled);
    
    // Info label
    label *info_label = create_label();
    info_label->text = "💡 TIPS:\n• Click group buttons to add fish\n• Hover over fish to make them flee!\n• Predators scare normal fish";
    info_label->color = g_strdup("#aaa");
    info_label->style = g_strdup("italic");
    update_label(info_label);
    add_to_box(sidebar, info_label->label);
    
    // Spacer
    box *spacer = create_vbox(0, FALSE);
    spacer->expand = TRUE;
    update_box(spacer);
    add_to_box(sidebar, get_box_widget(spacer));
    
    // Create drawing area
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, WINDOW_WIDTH - 260, WINDOW_HEIGHT);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_mouse_move), NULL);
    gtk_widget_add_events(drawing_area, GDK_POINTER_MOTION_MASK);
    
    // Assemble UI
    add_to_box(main_hbox, get_box_widget(sidebar));
    add_to_box(main_hbox, drawing_area);
    add_to_window(main_window, get_box_widget(main_hbox));
    
    // Load background
    current_width = WINDOW_WIDTH;
    current_height = WINDOW_HEIGHT;
    load_background();
    
    // Start animation
    animation_timeout_id = g_timeout_add(TIMEOUT_MS, animation_tick, NULL);
    
    display_window(main_window);
    MainStart();
    
    // Cleanup
    if (animation_timeout_id) g_source_remove(animation_timeout_id);
    for (int i = 0; i < group_count; i++) {
        if (groups[i].pixbuf_cache) g_object_unref(groups[i].pixbuf_cache);
        if (groups[i].surface) cairo_surface_destroy(groups[i].surface);
        g_free(groups[i].image_path);
        g_free(groups[i].group_name);
    }
    if (background_surface) cairo_surface_destroy(background_surface);
    
    ProgramEnd();
    return 0;
}