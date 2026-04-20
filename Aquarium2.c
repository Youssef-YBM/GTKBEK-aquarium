#include "structures.h"
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_FISH 200
#define MAX_GROUPS 20
#define WINDOW_WIDTH 1366
#define WINDOW_HEIGHT 768
#define TIMEOUT_MS 33  // ~30 FPS
#define AVOID_DISTANCE 150
#define REGROUP_DELAY 2000  // ms to regroup after fleeing
#define MAX_SPEED_MULTIPLIER 4.0

typedef struct {
    double x, y;           // position
    double vx, vy;         // velocity
    int group_id;          // which group this fish belongs to
    int is_leader;         // is this the group leader?
    int is_predator;       // is this a predator?
    int aggression_level;  // higher = others flee more
    int width, height;     // image dimensions
    double angle;          // current rotation angle
    int fleeing;
    guint flee_timeout_id;
    int active;
} Fish;

typedef struct {
    int group_id;
    char *image_path;
    double speed;
    int member_count;
    int members_indices[MAX_FISH];  // store indices instead of pointers
    GdkPixbuf *pixbuf_cache;
    cairo_surface_t *surface;
    int img_width, img_height;
    int leader_index;
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
int animation_timeout_id = 0;
int hover_active = 0;
double hover_x = 0, hover_y = 0;
int is_fullscreen = 0;
GdkPixbuf *background_pixbuf = NULL;
cairo_surface_t *background_surface = NULL;
int current_width = WINDOW_WIDTH;
int current_height = WINDOW_HEIGHT;

// Function prototypes
void add_fish(int group_id, int is_predator, int aggression_level);
void create_fish_group(const char *image_path, double speed);
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

// Create a beautiful underwater background
void load_background() {
    // Create a gradient background programmatically
    if (background_surface) {
        cairo_surface_destroy(background_surface);
    }
    
    background_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, current_width, current_height);
    cairo_t *cr = cairo_create(background_surface);
    
    // Create vertical gradient from light blue to deep blue
    cairo_pattern_t *gradient = cairo_pattern_create_linear(0, 0, 0, current_height);
    cairo_pattern_add_color_stop_rgb(gradient, 0.0, 0.2, 0.5, 0.8);  // Top: light blue
    cairo_pattern_add_color_stop_rgb(gradient, 0.3, 0.1, 0.3, 0.6);  // Middle: teal
    cairo_pattern_add_color_stop_rgb(gradient, 0.7, 0.05, 0.15, 0.4); // Deep: navy
    cairo_pattern_add_color_stop_rgb(gradient, 1.0, 0.02, 0.05, 0.2);  // Bottom: dark blue
    
    cairo_rectangle(cr, 0, 0, current_width, current_height);
    cairo_set_source(cr, gradient);
    cairo_fill(cr);
    cairo_pattern_destroy(gradient);
    
    // Add light rays from top
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.8, 0.05);
    for (int i = 0; i < 20; i++) {
        double x = (rand() % current_width);
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x + (rand() % 200) - 100, current_height);
        cairo_line_to(cr, x + (rand() % 100) - 50, current_height);
        cairo_fill(cr);
    }
    
    cairo_destroy(cr);
}

void draw_bubbles(cairo_t *cr, int width, int height) {
    static int bubble_frame = 0;
    bubble_frame++;
    
    for (int i = 0; i < 30; i++) {
        int x = (i * 137 + bubble_frame * 3) % width;
        int y = (i * 73 + bubble_frame) % height;
        int size = 3 + (i % 7);
        
        cairo_set_source_rgba(cr, 0.8, 0.9, 1.0, 0.3);
        cairo_arc(cr, x, y, size, 0, 2 * M_PI);
        cairo_fill(cr);
        
        // Add highlight
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
        cairo_arc(cr, x - size/3, y - size/3, size/4, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}

cairo_surface_t* load_fish_surface(const char *path, int *out_width, int *out_height, int target_size) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);
    if (!pixbuf) {
        // Create a colored rectangle as fallback
        g_printerr("Failed to load image %s: %s\n", path, error->message);
        g_error_free(error);
        
        // Create a simple colored fish shape
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_size, target_size);
        cairo_t *cr = cairo_create(surface);
        
        // Draw a simple fish shape
        cairo_set_source_rgb(cr, 1.0, 0.6, 0.2);
        cairo_arc(cr, target_size/2, target_size/2, target_size/2, 0, 2 * M_PI);
        cairo_fill(cr);
        
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_arc(cr, target_size/3, target_size/3, target_size/10, 0, 2 * M_PI);
        cairo_fill(cr);
        
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

void create_fish_group(const char *image_path, double speed) {
    if (group_count >= MAX_GROUPS) return;
    
    groups[group_count].group_id = group_count;
    groups[group_count].image_path = g_strdup(image_path);
    groups[group_count].speed = speed;
    groups[group_count].member_count = 0;
    groups[group_count].leader_index = -1;
    
    // Load surface for drawing
    groups[group_count].surface = load_fish_surface(image_path, 
                                                     &groups[group_count].img_width,
                                                     &groups[group_count].img_height, 45);
    
    // Cache pixbuf for potential later use
    GError *error = NULL;
    groups[group_count].pixbuf_cache = gdk_pixbuf_new_from_file(image_path, &error);
    if (error) {
        g_error_free(error);
        groups[group_count].pixbuf_cache = NULL;
    }
    
    group_count++;
}

void add_fish(int group_id, int is_predator, int aggression_level) {
    if (fish_count >= MAX_FISH) return;
    if (group_id >= group_count) return;
    
    Fish *f = &fishes[fish_count];
    f->group_id = group_id;
    f->is_predator = is_predator;
    f->aggression_level = aggression_level;
    f->is_leader = 0;
    f->fleeing = 0;
    f->flee_timeout_id = 0;
    f->active = 1;
    f->width = groups[group_id].img_width;
    f->height = groups[group_id].img_height;
    f->angle = 0;
    
    // Random position within screen bounds
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
            // Fish is fleeing from cursor - move away rapidly
            double dx = f->x - hover_x;
            double dy = f->y - hover_y;
            double dist = sqrt(dx*dx + dy*dy);
            if (dist > 0.01 && dist < AVOID_DISTANCE * 2) {
                double angle = atan2(dy, dx);
                f->vx = cos(angle) * g->speed * MAX_SPEED_MULTIPLIER;
                f->vy = sin(angle) * g->speed * MAX_SPEED_MULTIPLIER;
            }
        } else if (!f->is_leader && g->leader_index >= 0 && g->leader_index < g->member_count) {
            // Follow leader
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
                    // Small random movement when close
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
            // Leader moves with occasional direction changes
            if (rand() % 100 < 3) {
                double angle = (rand() % 360) * M_PI / 180.0;
                f->vx = cos(angle) * g->speed;
                f->vy = sin(angle) * g->speed;
            }
            
            // Add small random variations
            f->vx += ((rand() % 100) - 50) / 1000.0;
            f->vy += ((rand() % 100) - 50) / 1000.0;
        }
        
        // Update position
        f->x += f->vx;
        f->y += f->vy;
        
        // Wrap around screen (like snake game) with padding
        int padding = 100;
        if (f->x < -padding) f->x = current_width + padding;
        if (f->x > current_width + padding) f->x = -padding;
        if (f->y < -padding) f->y = current_height + padding;
        if (f->y > current_height + padding) f->y = -padding;
        
        // Limit velocities
        double len = sqrt(f->vx*f->vx + f->vy*f->vy);
        double max_speed = f->fleeing ? g->speed * MAX_SPEED_MULTIPLIER : g->speed * 1.2;
        if (len > max_speed && max_speed > 0) {
            f->vx = (f->vx / len) * max_speed;
            f->vy = (f->vy / len) * max_speed;
        }
        
        // Update angle for drawing
        f->angle = atan2(f->vy, f->vx);
    }
}

void flee_from_hover(int fish_idx) {
    if (fish_idx < 0 || fish_idx >= fish_count) return;
    
    Fish *trigger = &fishes[fish_idx];
    if (!trigger->active) return;
    
    // All fish with lower aggression will flee
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
    // Get current widget size
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    
    if (allocation.width != current_width || allocation.height != current_height) {
        current_width = allocation.width;
        current_height = allocation.height;
        load_background();
    }
    
    // Draw background
    if (background_surface) {
        cairo_set_source_surface(cr, background_surface, 0, 0);
        cairo_paint(cr);
    } else {
        // Fallback background
        cairo_set_source_rgb(cr, 0.1, 0.2, 0.3);
        cairo_paint(cr);
    }
    
    // Draw bubbles
    draw_bubbles(cr, current_width, current_height);
    
    // Draw all fish
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (!f->active) continue;
        if (f->group_id >= group_count) continue;
        
        FishGroup *g = &groups[f->group_id];
        if (!g->surface) continue;
        
        cairo_save(cr);
        
        // Translate to fish position
        cairo_translate(cr, f->x, f->y);
        
        // Rotate based on movement direction
        cairo_rotate(cr, f->angle);
        
        // Flip horizontally if moving left (for better visual)
        if (f->vx < 0) {
            cairo_scale(cr, -1, 1);
        }
        
        // Draw fish image
        cairo_set_source_surface(cr, g->surface, -f->width/2, -f->height/2);
        cairo_paint(cr);
        
        cairo_restore(cr);
        
        // Draw leader crown
        if (f->is_leader && !f->fleeing) {
            cairo_save(cr);
            cairo_set_source_rgb(cr, 1.0, 0.85, 0.2);
            cairo_arc(cr, f->x, f->y - f->height/2 - 5, 6, 0, 2 * M_PI);
            cairo_fill(cr);
            
            cairo_set_source_rgb(cr, 1.0, 0.6, 0.0);
            cairo_arc(cr, f->x, f->y - f->height/2 - 5, 3, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        
        // Draw predator glow
        if (f->is_predator) {
            cairo_save(cr);
            cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 0.3);
            cairo_arc(cr, f->x, f->y, f->width/2 + 10, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        
        // Draw fleeing effect (exclamation marks)
        if (f->fleeing) {
            cairo_save(cr);
            cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
            cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 16);
            cairo_move_to(cr, f->x + 10, f->y - 15);
            cairo_show_text(cr, "!");
            cairo_restore(cr);
        }
    }
    
    return FALSE;
}

gboolean on_mouse_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    hover_x = event->x;
    hover_y = event->y;
    hover_active = 1;
    
    // Check if mouse is over any fish
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (!f->active) continue;
        
        double dx = f->x - hover_x;
        double dy = f->y - hover_y;
        double dist = sqrt(dx*dx + dy*dy);
        if (dist < 35) {
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

// Callback for window resize
void on_window_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    current_width = event->width;
    current_height = event->height;
    load_background();
}

// Sidebar button callbacks
void on_add_normal_fish(GtkWidget *widget, gpointer data) {
    static int group_selector = 0;
    if (group_count > 0) {
        group_selector = (group_selector + 1) % group_count;
        add_fish(group_selector, 0, 1);
    }
}

void on_add_predator_fish(GtkWidget *widget, gpointer data) {
    static int group_selector = 0;
    if (group_count > 0) {
        group_selector = (group_selector + 1) % group_count;
        add_fish(group_selector, 1, 5);
    }
}

void on_delete_fish(GtkWidget *widget, gpointer data) {
    if (fish_count > 0) {
        delete_fish(fish_count - 1);
    }
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
        
        // Dialog for speed
        GtkWidget *speed_dialog = gtk_dialog_new_with_buttons("Set Fish Speed",
                                                              GTK_WINDOW(main_window->win),
                                                              GTK_DIALOG_MODAL,
                                                              "_OK", GTK_RESPONSE_OK,
                                                              NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(speed_dialog));
        GtkWidget *spin = gtk_spin_button_new_with_range(0.5, 8.0, 0.5);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 2.0);
        
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Fish Speed:"), FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), spin, FALSE, FALSE, 5);
        gtk_container_add(GTK_CONTAINER(content), vbox);
        gtk_widget_show_all(content);
        
        if (gtk_dialog_run(GTK_DIALOG(speed_dialog)) == GTK_RESPONSE_OK) {
            double speed = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
            create_fish_group(filename, speed);
            
            // Add to sidebar
            char label_text[256];
            snprintf(label_text, sizeof(label_text), "🐟 Group %d", group_count);
            label *group_label = create_label();
            group_label->text = g_strdup(label_text);
            group_label->color = g_strdup("#87CEEB");
            update_label(group_label);
            add_to_box(sidebar, group_label->label);
        }
        gtk_widget_destroy(speed_dialog);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    
    ProgramStart();
    
    // Create main window
    main_window = create_window();
    main_window->title = "🐠 Advanced Aquarium Simulator";
    main_window->width = WINDOW_WIDTH;
    main_window->height = WINDOW_HEIGHT;
    main_window->resizable = TRUE;
    main_window->bg_color = g_strdup("#0a1a2a");
    update_window(main_window);
    
    // Connect window events
    g_signal_connect(main_window->win, "configure-event", G_CALLBACK(on_window_configure), NULL);
    
    // Create main horizontal box
    main_hbox = create_hbox(0, FALSE);
    
    // Create sidebar
    sidebar = create_vbox(10, FALSE);
    sidebar->width = 220;
    sidebar->bg_color = g_strdup("rgba(0, 0, 0, 0.75)");
    sidebar->margin_top = sidebar->margin_bottom = sidebar->margin_left = sidebar->margin_right = 10;
    update_box(sidebar);
    
    // Sidebar title with icon
    label *title_label = create_label();
    title_label->text = "🐟 AQUARIUM CONTROLS";
    title_label->color = g_strdup("#FFD700");
    title_label->style = g_strdup("bold");
    title_label->font_family = g_strdup("Arial Black");
    update_label(title_label);
    add_to_box(sidebar, title_label->label);
    
    // Separator
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    add_to_box(sidebar, sep);
    
    // Buttons with icons
    button *add_fish_btn = create_button();
    add_fish_btn->label = "🐟 Add Normal Fish";
    add_fish_btn->bg_color = g_strdup("#2ecc71");
    add_fish_btn->click_callback = on_add_normal_fish;
    update_button(add_fish_btn);
    add_to_box(sidebar, add_fish_btn->button);
    
    button *add_predator_btn = create_button();
    add_predator_btn->label = "🦈 Add Predator";
    add_predator_btn->bg_color = g_strdup("#e74c3c");
    add_predator_btn->click_callback = on_add_predator_fish;
    update_button(add_predator_btn);
    add_to_box(sidebar, add_predator_btn->button);
    
    button *add_group_btn = create_button();
    add_group_btn->label = "📁 Create Fish Group";
    add_group_btn->bg_color = g_strdup("#3498db");
    add_group_btn->click_callback = on_add_fish_group;
    update_button(add_group_btn);
    add_to_box(sidebar, add_group_btn->button);
    
    button *delete_btn = create_button();
    delete_btn->label = "🗑️ Delete Last Fish";
    delete_btn->bg_color = g_strdup("#e67e22");
    delete_btn->click_callback = on_delete_fish;
    update_button(delete_btn);
    add_to_box(sidebar, delete_btn->button);
    
    button *fullscreen_btn = create_button();
    fullscreen_btn->label = "🖥️ Toggle Fullscreen (F11)";
    fullscreen_btn->bg_color = g_strdup("#9b59b6");
    fullscreen_btn->click_callback = toggle_fullscreen;
    update_button(fullscreen_btn);
    add_to_box(sidebar, fullscreen_btn->button);
    
    // Stats label
    label *stats_label = create_label();
    stats_label->text = "✨ Hover over fish to make them flee!";
    stats_label->color = g_strdup("#aaa");
    stats_label->style = g_strdup("italic");
    update_label(stats_label);
    add_to_box(sidebar, stats_label->label);
    
    // Spacer
    box *spacer = create_vbox(0, FALSE);
    spacer->expand = TRUE;
    update_box(spacer);
    add_to_box(sidebar, get_box_widget(spacer));
    
    // Create drawing area
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, WINDOW_WIDTH - 220, WINDOW_HEIGHT);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_mouse_move), NULL);
    gtk_widget_add_events(drawing_area, GDK_POINTER_MOTION_MASK);
    
    // Assemble UI
    add_to_box(main_hbox, get_box_widget(sidebar));
    add_to_box(main_hbox, drawing_area);
    add_to_window(main_window, get_box_widget(main_hbox));
    
    // Load default fish images (use system icons or create colored shapes)
    create_fish_group("/usr/share/icons/Adwaita/48x48/places/start-here.png", 2.0);
    create_fish_group("/usr/share/icons/Adwaita/48x48/actions/go-home.png", 1.8);
    create_fish_group("/usr/share/icons/Adwaita/48x48/status/face-smile.png", 2.2);
    
    // Add default fish
    for (int i = 0; i < 5; i++) {
        add_fish(0, 0, 1);
    }
    add_fish(1, 0, 2);
    add_fish(1, 1, 5);  // predator
    add_fish(2, 0, 1);
    
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
    }
    if (background_surface) cairo_surface_destroy(background_surface);
    
    ProgramEnd();
    return 0;
}