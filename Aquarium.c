#include "structures.h"
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_FISH 100
#define MAX_GROUPS 10
#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800
#define TIMEOUT_MS 33  // ~30 FPS
#define AVOID_DISTANCE 150
#define REGROUP_DELAY 3000  // ms to regroup after fleeing

typedef struct {
    double x, y;           // position
    double vx, vy;         // velocity
    int group_id;          // which group this fish belongs to
    int is_leader;         // is this the group leader?
    int is_predator;       // is this a predator?
    int aggression_level;  // higher = others flee more
    GtkWidget *image;      // fish image widget
    double target_x, target_y; // for smooth movement
    int fleeing;
    guint flee_timeout_id;
} Fish;

typedef struct {
    int group_id;
    char *image_path;
    double speed;
    int member_count;
    Fish *members[MAX_FISH];
    GdkPixbuf *pixbuf_cache;  // cached pixbuf for this group
    int leader_id;             // index of leader in members array
} FishGroup;

// Global variables
Fish fishes[MAX_FISH];
FishGroup groups[MAX_GROUPS];
int fish_count = 0;
int group_count = 0;
GtkWidget *drawing_area;
window *main_window;
box *sidebar;
box *main_area;
box *fish_container;
cairo_surface_t *fish_surfaces[MAX_GROUPS];
int fish_surface_count = 0;
guint animation_timeout_id = 0;
int hover_fish_index = -1;
double hover_x = 0, hover_y = 0;
guint regroup_timeout_id = 0;
int fleeing_fish[MAX_FISH];

// Function prototypes
void add_fish(int group_id, int is_predator, int aggression_level);
void create_fish_group(const char *image_path, double speed);
void update_animation();
gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean on_mouse_move(GtkWidget *widget, GdkEventMotion *event, gpointer data);
gboolean animation_tick(gpointer data);
void regroup_fish(gpointer data);
void flee_from_hover(int fish_idx);
void delete_fish(int fish_idx);

// Helper: load image as surface for fast drawing
cairo_surface_t* load_fish_surface(const char *path, int width, int height) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);
    if (!pixbuf) {
        g_printerr("Failed to load image %s: %s\n", path, error->message);
        g_error_free(error);
        return NULL;
    }
    
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, width, height, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);
    
    cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf(scaled, 1.0, NULL);
    g_object_unref(scaled);
    
    return surface;
}

void create_fish_group(const char *image_path, double speed) {
    if (group_count >= MAX_GROUPS) return;
    
    groups[group_count].group_id = group_count;
    groups[group_count].image_path = g_strdup(image_path);
    groups[group_count].speed = speed;
    groups[group_count].member_count = 0;
    groups[group_count].leader_id = -1;
    
    // Load and cache pixbuf
    GError *error = NULL;
    groups[group_count].pixbuf_cache = gdk_pixbuf_new_from_file(image_path, &error);
    if (error) {
        g_printerr("Error: %s\n", error->message);
        g_error_free(error);
    }
    
    // Load surface for drawing
    fish_surfaces[group_count] = load_fish_surface(image_path, 40, 30);
    
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
    
    // Random position within screen bounds
    f->x = rand() % (WINDOW_WIDTH - 100) + 50;
    f->y = rand() % (WINDOW_HEIGHT - 100) + 50;
    
    // Random direction
    double angle = (rand() % 360) * M_PI / 180.0;
    f->vx = cos(angle) * groups[group_id].speed;
    f->vy = sin(angle) * groups[group_id].speed;
    
    f->target_x = f->x;
    f->target_y = f->y;
    
    // Add to group members list
    groups[group_id].members[groups[group_id].member_count] = f;
    f->is_leader = (groups[group_id].member_count == 0);  // First fish is leader
    if (f->is_leader) {
        groups[group_id].leader_id = groups[group_id].member_count;
    }
    
    fish_count++;
    groups[group_id].member_count++;
}

void update_fish_movement() {
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        FishGroup *g = &groups[f->group_id];
        
        if (f->fleeing) {
            // Fish is fleeing from cursor - move away rapidly
            double dx = f->x - hover_x;
            double dy = f->y - hover_y;
            double dist = sqrt(dx*dx + dy*dy);
            if (dist > 0.01) {
                double angle = atan2(dy, dx);
                f->vx = cos(angle) * g->speed * 3.0;
                f->vy = sin(angle) * g->speed * 3.0;
            }
        } else if (!f->is_leader && groups[f->group_id].leader_id >= 0) {
            // Follow leader
            Fish *leader = groups[f->group_id].members[groups[f->group_id].leader_id];
            if (leader) {
                double dx = leader->x - f->x;
                double dy = leader->y - f->y;
                double dist = sqrt(dx*dx + dy*dy);
                if (dist > 50) {
                    double angle = atan2(dy, dx);
                    double target_vx = cos(angle) * g->speed;
                    double target_vy = sin(angle) * g->speed;
                    f->vx = f->vx * 0.9 + target_vx * 0.1;
                    f->vy = f->vy * 0.9 + target_vy * 0.1;
                } else {
                    // Random small movement when close
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
            // Leader moves randomly but smoothly
            if (rand() % 100 < 5) {
                double angle = (rand() % 360) * M_PI / 180.0;
                f->vx = cos(angle) * g->speed;
                f->vy = sin(angle) * g->speed;
            }
        }
        
        // Update position
        f->x += f->vx;
        f->y += f->vy;
        
        // Wrap around screen (like snake game)
        if (f->x < -50) f->x = WINDOW_WIDTH + 50;
        if (f->x > WINDOW_WIDTH + 50) f->x = -50;
        if (f->y < -50) f->y = WINDOW_HEIGHT + 50;
        if (f->y > WINDOW_HEIGHT + 50) f->y = -50;
        
        // Prevent extreme velocities
        double len = sqrt(f->vx*f->vx + f->vy*f->vy);
        double max_speed = f->fleeing ? g->speed * 4.0 : g->speed * 1.5;
        if (len > max_speed) {
            f->vx = (f->vx / len) * max_speed;
            f->vy = (f->vy / len) * max_speed;
        }
    }
}

gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    // Clear background (water color)
    cairo_set_source_rgb(cr, 0.1, 0.2, 0.3);
    cairo_paint(cr);
    
    // Draw all fish
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        FishGroup *g = &groups[f->group_id];
        
        if (fish_surfaces[f->group_id]) {
            // Determine direction for flipping
            double angle = atan2(f->vy, f->vx);
            
            cairo_save(cr);
            cairo_translate(cr, f->x, f->y);
            cairo_rotate(cr, angle);
            
            // Flip horizontally if moving left
            if (f->vx < 0) {
                cairo_scale(cr, -1, 1);
            }
            
            cairo_set_source_surface(cr, fish_surfaces[f->group_id], -20, -15);
            cairo_paint(cr);
            cairo_restore(cr);
            
            // Draw leader indicator (crown) if this is leader and not fleeing
            if (f->is_leader && !f->fleeing) {
                cairo_save(cr);
                cairo_set_source_rgb(cr, 1, 0.8, 0);
                cairo_arc(cr, f->x, f->y - 20, 5, 0, 2 * M_PI);
                cairo_fill(cr);
                cairo_restore(cr);
            }
            
            // Draw predator indicator (red glow)
            if (f->is_predator) {
                cairo_save(cr);
                cairo_set_source_rgba(cr, 1, 0, 0, 0.3);
                cairo_arc(cr, f->x, f->y, 25, 0, 2 * M_PI);
                cairo_fill(cr);
                cairo_restore(cr);
            }
        }
    }
    
    return FALSE;
}

gboolean on_mouse_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    hover_x = event->x;
    hover_y = event->y;
    
    // Check if mouse is over any fish
    int found = -1;
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        double dx = f->x - hover_x;
        double dy = f->y - hover_y;
        double dist = sqrt(dx*dx + dy*dy);
        if (dist < 30) {
            found = i;
            break;
        }
    }
    
    // If hovering over a new fish, trigger flee
    if (found != -1 && hover_fish_index != found) {
        hover_fish_index = found;
        flee_from_hover(found);
    } else if (found == -1 && hover_fish_index != -1) {
        hover_fish_index = -1;
    }
    
    return FALSE;
}

void flee_from_hover(int fish_idx) {
    if (fish_idx < 0 || fish_idx >= fish_count) return;
    
    Fish *trigger = &fishes[fish_idx];
    
    // All fish that are lower aggression (or all if predator) will flee
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        
        // Fish flees if: it's not a predator AND (trigger is predator OR trigger has higher aggression)
        if (!f->is_predator && (trigger->is_predator || trigger->aggression_level > f->aggression_level)) {
            if (!f->fleeing) {
                f->fleeing = 1;
                
                // Set timeout to stop fleeing after REGROUP_DELAY
                if (f->flee_timeout_id) {
                    g_source_remove(f->flee_timeout_id);
                }
                struct {
                    int fish_idx;
                } *data = malloc(sizeof(*data));
                data->fish_idx = i;
                f->flee_timeout_id = g_timeout_add(REGROUP_DELAY, (GSourceFunc)regroup_fish, data);
            }
        }
    }
}

void regroup_fish(gpointer data) {
    struct {
        int fish_idx;
    } *d = (struct { int fish_idx; }*)data;
    
    if (d->fish_idx >= 0 && d->fish_idx < fish_count) {
        fishes[d->fish_idx].fleeing = 0;
        if (fishes[d->fish_idx].flee_timeout_id) {
            fishes[d->fish_idx].flee_timeout_id = 0;
        }
    }
    free(d);
    return FALSE;
}

void delete_fish(int fish_idx) {
    if (fish_idx < 0 || fish_idx >= fish_count) return;
    
    Fish *f = &fishes[fish_idx];
    FishGroup *g = &groups[f->group_id];
    
    // Remove from group members
    int group_pos = -1;
    for (int i = 0; i < g->member_count; i++) {
        if (g->members[i] == f) {
            group_pos = i;
            break;
        }
    }
    
    if (group_pos >= 0) {
        for (int i = group_pos; i < g->member_count - 1; i++) {
            g->members[i] = g->members[i + 1];
        }
        g->member_count--;
        
        // If we deleted the leader, assign new leader
        if (group_pos == g->leader_id && g->member_count > 0) {
            g->leader_id = 0;
            g->members[0]->is_leader = 1;
        } else if (g->leader_id > group_pos) {
            g->leader_id--;
        }
    }
    
    // Shift remaining fish in global array
    for (int i = fish_idx; i < fish_count - 1; i++) {
        fishes[i] = fishes[i + 1];
    }
    fish_count--;
}

gboolean animation_tick(gpointer data) {
    update_fish_movement();
    gtk_widget_queue_draw(drawing_area);
    return TRUE;
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

void on_delete_last_fish(GtkWidget *widget, gpointer data) {
    if (fish_count > 0) {
        delete_fish(fish_count - 1);
    }
}

void on_add_fish_group(GtkWidget *widget, gpointer data) {
    // Create a file chooser dialog for fish image
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Fish Image",
                                                     GTK_WINDOW(main_window->win),
                                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                                     "_Cancel", GTK_RESPONSE_CANCEL,
                                                     "_Open", GTK_RESPONSE_ACCEPT,
                                                     NULL);
    
    // Add image filters
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images");
    gtk_file_filter_add_pixbuf_formats(filter);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        
        // Create dialog for speed
        GtkWidget *speed_dialog = gtk_dialog_new_with_buttons("Set Fish Speed",
                                                              GTK_WINDOW(main_window->win),
                                                              GTK_DIALOG_MODAL,
                                                              "_OK", GTK_RESPONSE_OK,
                                                              NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(speed_dialog));
        GtkWidget *spin = gtk_spin_button_new_with_range(0.5, 10.0, 0.5);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 2.0);
        gtk_label_set_text(GTK_LABEL(gtk_label_new("Speed (pixels/frame):")), "");
        gtk_box_pack_start(GTK_BOX(content), gtk_label_new("Speed (pixels/frame):"), FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(content), spin, FALSE, FALSE, 5);
        gtk_widget_show_all(content);
        
        if (gtk_dialog_run(GTK_DIALOG(speed_dialog)) == GTK_RESPONSE_OK) {
            double speed = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
            create_fish_group(filename, speed);
            
            // Add a label to sidebar showing new group
            char label_text[256];
            snprintf(label_text, sizeof(label_text), "Group %d: %s", group_count, filename);
            label *group_label = create_label();
            group_label->text = g_strdup(label_text);
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
    main_window->title = "Aquarium Simulator";
    main_window->width = WINDOW_WIDTH;
    main_window->height = WINDOW_HEIGHT;
    main_window->resizable = TRUE;
    update_window(main_window);
    
    // Create main horizontal box
    box *main_hbox = create_hbox(0, FALSE);
    
    // Create sidebar (left)
    sidebar = create_vbox(10, FALSE);
    sidebar->width = 200;
    sidebar->bg_color = g_strdup("rgba(0,0,0,0.7)");
    update_box(sidebar);
    
    // Sidebar title
    label *title_label = create_label();
    title_label->text = "Aquarium Controls";
    title_label->color = g_strdup("white");
    title_label->style = g_strdup("bold");
    title_label->font_family = g_strdup("Arial");
    update_label(title_label);
    add_to_box(sidebar, title_label->label);
    
    // Buttons
    button *add_fish_btn = create_button();
    add_fish_btn->label = "Add Normal Fish";
    add_fish_btn->bg_color = g_strdup("#2ecc71");
    add_fish_btn->click_callback = on_add_normal_fish;
    update_button(add_fish_btn);
    add_to_box(sidebar, add_fish_btn->button);
    
    button *add_predator_btn = create_button();
    add_predator_btn->label = "Add Predator";
    add_predator_btn->bg_color = g_strdup("#e74c3c");
    add_predator_btn->click_callback = on_add_predator_fish;
    update_button(add_predator_btn);
    add_to_box(sidebar, add_predator_btn->button);
    
    button *add_group_btn = create_button();
    add_group_btn->label = "Create New Fish Group";
    add_group_btn->bg_color = g_strdup("#3498db");
    add_group_btn->click_callback = on_add_fish_group;
    update_button(add_group_btn);
    add_to_box(sidebar, add_group_btn->button);
    
    button *delete_btn = create_button();
    delete_btn->label = "Delete Last Fish";
    delete_btn->bg_color = g_strdup("#f39c12");
    delete_btn->click_callback = on_delete_last_fish;
    update_button(delete_btn);
    add_to_box(sidebar, delete_btn->button);
    
    // Spacer
    box *spacer = create_vbox(0, FALSE);
    spacer->expand = TRUE;
    update_box(spacer);
    add_to_box(sidebar, get_box_widget(spacer));
    
    // Add some default fish groups with default images (using system icons as fallback)
    create_fish_group("/usr/share/icons/Adwaita/256x256/places/fish.png", 2.0);
    create_fish_group("/usr/share/icons/Adwaita/256x256/places/user-desktop.png", 1.5);
    
    // Add some default fish
    add_fish(0, 0, 1);
    add_fish(0, 0, 1);
    add_fish(0, 0, 1);
    add_fish(1, 0, 2);
    add_fish(1, 1, 5);  // predator
    
    // Create drawing area
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, WINDOW_WIDTH - 200, WINDOW_HEIGHT);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_mouse_move), NULL);
    gtk_widget_add_events(drawing_area, GDK_POINTER_MOTION_MASK);
    
    // Assemble UI
    add_to_box(main_hbox, get_box_widget(sidebar));
    add_to_box(main_hbox, drawing_area);
    add_to_window(main_window, get_box_widget(main_hbox));
    
    // Start animation
    animation_timeout_id = g_timeout_add(TIMEOUT_MS, animation_tick, NULL);
    
    display_window(main_window);
    MainStart();
    
    // Cleanup
    if (animation_timeout_id) g_source_remove(animation_timeout_id);
    for (int i = 0; i < group_count; i++) {
        if (groups[i].pixbuf_cache) g_object_unref(groups[i].pixbuf_cache);
        if (fish_surfaces[i]) cairo_surface_destroy(fish_surfaces[i]);
        g_free(groups[i].image_path);
    }
    
    ProgramEnd();
    return 0;
}