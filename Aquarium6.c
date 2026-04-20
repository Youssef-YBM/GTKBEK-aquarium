#include "structures.h"
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define MAX_FISH 200
#define MAX_GROUPS 20
#define DEFAULT_WIDTH 1366
#define DEFAULT_HEIGHT 768
#define TIMEOUT_MS 33
#define AVOID_DISTANCE 150
#define REGROUP_DELAY 2000
#define MAX_SPEED_MULTIPLIER 4.0
#define MAX_PARTICLES 300

typedef struct {
    double x, y;
    double vx, vy;
    int life;
    int max_life;
    int type;
    double size;
    guint8 r, g, b;
} Particle;

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
    double hunger;
    double energy;
    char name[50];
} Fish;

typedef struct {
    int group_id;
    char *image_path;
    char *group_name;
    double speed;
    int member_count;
    int members_indices[MAX_FISH];
    cairo_surface_t *surface;
    cairo_surface_t *surface_flipped;
    int img_width, img_height;
    int leader_index;
    int is_predator_group;
} FishGroup;

// Global variables
Fish fishes[MAX_FISH];
FishGroup groups[MAX_GROUPS];
Particle particles[MAX_PARTICLES];
int fish_count = 0;
int group_count = 0;
GtkWidget *drawing_area;
GtkWidget *main_window_widget;
box *sidebar;
box *main_hbox;
box *groups_list_box;
int animation_timeout_id = 0;
double hover_x = 0, hover_y = 0;
int hover_active = 0;
int is_fullscreen = 0;
cairo_surface_t *background_surface = NULL;
int current_width = DEFAULT_WIDTH;
int current_height = DEFAULT_HEIGHT;
int selected_fish_index = -1;
double camera_x = 0, camera_y = 0;
double zoom = 1.0;
GtkWidget *stats_label;
int dragging = 0;
double drag_start_x = 0, drag_start_y = 0;

// Function prototypes
void add_fish(int group_id);
void create_fish_group(const char *image_path, const char *group_name, double speed, int is_predator);
void update_fish_movement();
gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean on_mouse_move(GtkWidget *widget, GdkEventMotion *event, gpointer data);
gboolean on_mouse_button(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean on_mouse_release(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean animation_tick(gpointer data);
gboolean regroup_fish(gpointer data);
void flee_from_hover(int fish_idx);
void delete_fish(int fish_idx);
void load_background();
void toggle_fullscreen();
void refresh_groups_list();
void on_add_fish(GtkWidget *widget, gpointer data);
void on_add_predator_group(GtkWidget *widget, gpointer data);
void on_add_normal_group(GtkWidget *widget, gpointer data);
void on_delete_fish(GtkWidget *widget, gpointer data);
void on_feed_fish(GtkWidget *widget, gpointer data);
void on_make_predator(GtkWidget *widget, gpointer data);
void on_make_normal(GtkWidget *widget, gpointer data);
void update_stats();
void add_particle(double x, double y, int type);
void update_particles();
void draw_particles(cairo_t *cr);
gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data);
void on_window_resize(GtkWidget *widget, GdkEventConfigure *event, gpointer data);

void add_particle(double x, double y, int type) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].life) {
            particles[i].x = x;
            particles[i].y = y;
            particles[i].vx = ((rand() % 60) - 30) / 20.0;
            particles[i].vy = ((rand() % 60) - 30) / 20.0;
            particles[i].life = 20 + rand() % 20;
            particles[i].max_life = particles[i].life;
            particles[i].type = type;
            particles[i].size = 2 + (rand() % 5);
            
            if (type == 0) {
                particles[i].r = 200;
                particles[i].g = 220;
                particles[i].b = 255;
                particles[i].vy = -2;
            } else if (type == 1) {
                particles[i].r = 255;
                particles[i].g = 200;
                particles[i].b = 100;
            } else if (type == 2) {
                particles[i].r = 255;
                particles[i].g = 255;
                particles[i].b = 255;
                particles[i].size = 6;
            } else if (type == 3) {
                particles[i].r = 200;
                particles[i].g = 150;
                particles[i].b = 100;
                particles[i].vy = -1;
            }
            break;
        }
    }
}

void update_particles() {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].life--;
            
            if (particles[i].type == 0) {
                particles[i].vy -= 0.1;
            }
            
            if (particles[i].y < -100 || particles[i].y > current_height + 100 ||
                particles[i].x < -100 || particles[i].x > current_width + 100) {
                particles[i].life = 0;
            }
        }
    }
}

void draw_particles(cairo_t *cr) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            double alpha = (double)particles[i].life / particles[i].max_life;
            cairo_set_source_rgba(cr, particles[i].r/255.0, particles[i].g/255.0, 
                                   particles[i].b/255.0, alpha * 0.7);
            
            if (particles[i].type == 2) {
                cairo_set_line_width(cr, 1);
                cairo_arc(cr, particles[i].x, particles[i].y, 
                         particles[i].size * (1 - alpha), 0, 2 * M_PI);
                cairo_stroke(cr);
            } else {
                cairo_arc(cr, particles[i].x, particles[i].y, 
                         particles[i].size * alpha, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        }
    }
}

void draw_dynamic_background(cairo_t *cr, int width, int height) {
    // Deep water gradient
    cairo_pattern_t *grad = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgb(grad, 0, 0.05, 0.15, 0.35);
    cairo_pattern_add_color_stop_rgb(grad, 0.5, 0.03, 0.10, 0.30);
    cairo_pattern_add_color_stop_rgb(grad, 1, 0.01, 0.05, 0.25);
    
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_set_source(cr, grad);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
    
    // Sandy floor (dynamic position)
    int sand_y = height * 0.85;
    cairo_set_source_rgb(cr, 0.76, 0.69, 0.50);
    cairo_rectangle(cr, 0, sand_y, width, height - sand_y);
    cairo_fill(cr);
    
    // Sand texture
    cairo_set_source_rgba(cr, 0.6, 0.5, 0.3, 0.3);
    for (int i = 0; i < width / 10; i++) {
        cairo_arc(cr, rand() % width, sand_y + (rand() % 50), 1 + (rand() % 3), 0, 2 * M_PI);
        cairo_fill(cr);
    }
    
    // Light rays
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.8, 0.05);
    for (int i = 0; i < 15; i++) {
        double x = (rand() % width);
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x + (rand() % 200) - 100, height);
        cairo_line_to(cr, x + (rand() % 100) - 50, height);
        cairo_fill(cr);
    }
    
    // Dynamic bubbles based on screen size
    cairo_set_source_rgba(cr, 0.7, 0.8, 1.0, 0.3);
    int bubble_count = width / 30;
    for (int i = 0; i < bubble_count; i++) {
        cairo_arc(cr, 30 + i * (width / bubble_count), height - 80 - (i % 5) * 40, 2 + (i % 6), 0, 2 * M_PI);
        cairo_fill(cr);
    }
}

void load_background() {
    if (background_surface) {
        cairo_surface_destroy(background_surface);
    }
    
    background_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, current_width, current_height);
    cairo_t *cr = cairo_create(background_surface);
    draw_dynamic_background(cr, current_width, current_height);
    cairo_destroy(cr);
}

cairo_surface_t* load_fish_surface(const char *path, int *out_width, int *out_height, int target_size, int flip) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);
    
    if (!pixbuf) {
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_size, target_size);
        cairo_t *cr = cairo_create(surface);
        
        cairo_set_source_rgb(cr, 0.9, 0.5, 0.2);
        cairo_save(cr);
        cairo_translate(cr, target_size/2, target_size/2);
        cairo_scale(cr, 1.0, 0.6);
        cairo_arc(cr, 0, 0, target_size/2, 0, 2 * M_PI);
        cairo_restore(cr);
        cairo_fill(cr);
        
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_arc(cr, target_size*0.7, target_size*0.35, target_size/8, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_arc(cr, target_size*0.72, target_size*0.33, target_size/12, 0, 2 * M_PI);
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
    
    if (flip) {
        GdkPixbuf *flipped = gdk_pixbuf_new(gdk_pixbuf_get_colorspace(scaled),
                                            gdk_pixbuf_get_has_alpha(scaled),
                                            gdk_pixbuf_get_bits_per_sample(scaled),
                                            *out_width, *out_height);
        gdk_pixbuf_copy_area(scaled, 0, 0, *out_width, *out_height, flipped, 0, 0);
        gdk_pixbuf_flip(flipped, TRUE);
        cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf(flipped, 1.0, NULL);
        g_object_unref(flipped);
        g_object_unref(scaled);
        g_object_unref(pixbuf);
        return surface;
    }
    
    cairo_surface_t *surface = gdk_cairo_surface_create_from_pixbuf(scaled, 1.0, NULL);
    g_object_unref(pixbuf);
    g_object_unref(scaled);
    
    return surface;
}

void create_fish_group(const char *image_path, const char *group_name, double speed, int is_predator) {
    if (group_count >= MAX_GROUPS) return;
    
    groups[group_count].group_id = group_count;
    groups[group_count].image_path = g_strdup(image_path);
    groups[group_count].group_name = g_strdup(group_name);
    groups[group_count].speed = speed;
    groups[group_count].member_count = 0;
    groups[group_count].leader_index = -1;
    groups[group_count].is_predator_group = is_predator;
    
    groups[group_count].surface = load_fish_surface(image_path, 
                                                     &groups[group_count].img_width,
                                                     &groups[group_count].img_height, 50, 0);
    groups[group_count].surface_flipped = load_fish_surface(image_path, 
                                                             &groups[group_count].img_width,
                                                             &groups[group_count].img_height, 50, 1);
    
    group_count++;
    refresh_groups_list();
    update_stats();
    
    for (int i = 0; i < 20; i++) {
        add_particle(current_width/2, current_height/2, 1);
    }
}

void add_fish(int group_id) {
    if (fish_count >= MAX_FISH) return;
    if (group_id >= group_count) return;
    
    Fish *f = &fishes[fish_count];
    f->group_id = group_id;
    f->is_predator = groups[group_id].is_predator_group;
    f->aggression_level = f->is_predator ? 5 : 2;
    f->is_leader = 0;
    f->fleeing = 0;
    f->flee_timeout_id = 0;
    f->active = 1;
    f->width = groups[group_id].img_width;
    f->height = groups[group_id].img_height;
    f->hunger = 0.3;
    f->energy = 0.8;
    sprintf(f->name, "%s #%d", groups[group_id].group_name, fish_count + 1);
    
    f->x = (rand() % (current_width - 200)) + 100;
    f->y = (rand() % (int)(current_height * 0.7)) + 50;
    
    double angle = (rand() % 360) * M_PI / 180.0;
    f->vx = cos(angle) * groups[group_id].speed;
    f->vy = sin(angle) * groups[group_id].speed;
    
    groups[group_id].members_indices[groups[group_id].member_count] = fish_count;
    f->is_leader = (groups[group_id].member_count == 0);
    if (f->is_leader) {
        groups[group_id].leader_index = groups[group_id].member_count;
    }
    
    fish_count++;
    groups[group_id].member_count++;
    update_stats();
    
    for (int i = 0; i < 5; i++) {
        add_particle(f->x, f->y, 2);
    }
}

void delete_fish(int fish_idx) {
    if (fish_idx < 0 || fish_idx >= fish_count) return;
    
    Fish *f = &fishes[fish_idx];
    if (f->group_id >= group_count) return;
    
    for (int i = 0; i < 10; i++) {
        add_particle(f->x, f->y, 1);
    }
    
    FishGroup *g = &groups[f->group_id];
    
    int group_pos = -1;
    for (int i = 0; i < g->member_count; i++) {
        if (g->members_indices[i] == fish_idx) {
            group_pos = i;
            break;
        }
    }
    
    if (group_pos >= 0) {
        for (int i = group_pos; i < g->member_count - 1; i++) {
            g->members_indices[i] = g->members_indices[i + 1];
        }
        g->member_count--;
        
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
    
    if (f->flee_timeout_id) {
        g_source_remove(f->flee_timeout_id);
    }
    
    for (int i = fish_idx; i < fish_count - 1; i++) {
        fishes[i] = fishes[i + 1];
    }
    fish_count--;
    
    for (int gid = 0; gid < group_count; gid++) {
        for (int i = 0; i < groups[gid].member_count; i++) {
            if (groups[gid].members_indices[i] > fish_idx) {
                groups[gid].members_indices[i]--;
            }
        }
    }
    
    update_stats();
}

void update_fish_movement() {
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (f->group_id >= group_count) continue;
        
        FishGroup *g = &groups[f->group_id];
        
        f->hunger += 0.001;
        f->energy -= 0.0005;
        
        if (f->fleeing) {
            double dx = f->x - hover_x;
            double dy = f->y - hover_y;
            double dist = sqrt(dx*dx + dy*dy);
            
            if (dist < AVOID_DISTANCE && dist > 0.01) {
                double angle = atan2(dy, dx);
                double speed_mult = MAX_SPEED_MULTIPLIER * (1.0 - dist/AVOID_DISTANCE);
                f->vx = cos(angle) * g->speed * speed_mult;
                f->vy = sin(angle) * g->speed * speed_mult;
                
                if (rand() % 10 == 0) {
                    add_particle(f->x, f->y, 3);
                }
            } else {
                f->fleeing = 0;
                if (f->flee_timeout_id) {
                    g_source_remove(f->flee_timeout_id);
                    f->flee_timeout_id = 0;
                }
                double len = sqrt(f->vx*f->vx + f->vy*f->vy);
                if (len > g->speed) {
                    f->vx = (f->vx / len) * g->speed;
                    f->vy = (f->vy / len) * g->speed;
                }
            }
        }
        else if (!f->is_leader && g->leader_index >= 0 && g->leader_index < g->member_count) {
            int leader_idx = g->members_indices[g->leader_index];
            if (leader_idx >= 0 && leader_idx < fish_count && fishes[leader_idx].active) {
                Fish *leader = &fishes[leader_idx];
                double dx = leader->x - f->x;
                double dy = leader->y - f->y;
                double dist = sqrt(dx*dx + dy*dy);
                
                if (dist > 60) {
                    double angle = atan2(dy, dx);
                    double target_vx = cos(angle) * g->speed;
                    double target_vy = sin(angle) * g->speed;
                    f->vx = f->vx * 0.95 + target_vx * 0.05;
                    f->vy = f->vy * 0.95 + target_vy * 0.05;
                } else if (dist < 30) {
                    double angle = atan2(dy, dx) + M_PI;
                    double target_vx = cos(angle) * g->speed * 0.3;
                    double target_vy = sin(angle) * g->speed * 0.3;
                    f->vx = f->vx * 0.95 + target_vx * 0.05;
                    f->vy = f->vy * 0.95 + target_vy * 0.05;
                } else {
                    f->vx += ((rand() % 100) - 50) / 800.0;
                    f->vy += ((rand() % 100) - 50) / 800.0;
                    double len = sqrt(f->vx*f->vx + f->vy*f->vy);
                    if (len > g->speed) {
                        f->vx = (f->vx / len) * g->speed;
                        f->vy = (f->vy / len) * g->speed;
                    }
                }
            }
        }
        else if (f->is_leader) {
            if (rand() % 100 < 2) {
                double angle = (rand() % 360) * M_PI / 180.0;
                f->vx = cos(angle) * g->speed;
                f->vy = sin(angle) * g->speed;
            }
            f->vx += ((rand() % 100) - 50) / 1000.0;
            f->vy += ((rand() % 100) - 50) / 1000.0;
            
            double len = sqrt(f->vx*f->vx + f->vy*f->vy);
            if (len > g->speed) {
                f->vx = (f->vx / len) * g->speed;
                f->vy = (f->vy / len) * g->speed;
            }
        }
        
        f->x += f->vx;
        f->y += f->vy;
        
        // Dynamic wrapping based on current screen size
        if (f->x < -100) f->x = current_width + 100;
        if (f->x > current_width + 100) f->x = -100;
        if (f->y < -100) f->y = current_height + 100;
        if (f->y > current_height + 100) f->y = -100;
        
        double len = sqrt(f->vx*f->vx + f->vy*f->vy);
        double max_speed = f->fleeing ? g->speed * MAX_SPEED_MULTIPLIER : g->speed * 1.5;
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
        }
        else if (trigger->aggression_level > f->aggression_level) {
            should_flee = 1;
        }
        else if (hover_active && !f->fleeing) {
            should_flee = 1;
        }
        
        if (should_flee && !f->fleeing) {
            f->fleeing = 1;
            
            for (int j = 0; j < 3; j++) {
                add_particle(f->x, f->y, 3);
            }
            
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
        add_particle(fishes[fish_idx].x, fishes[fish_idx].y, 2);
    }
    return FALSE;
}

gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    // Get current widget size dynamically
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    
    // Check if size changed
    if (allocation.width != current_width || allocation.height != current_height) {
        current_width = allocation.width;
        current_height = allocation.height;
        load_background();
        
        // Update stats to show new dimensions
        char dims[100];
        snprintf(dims, sizeof(dims), "Screen: %dx%d", current_width, current_height);
        gtk_label_set_text(GTK_LABEL(stats_label), dims);
    }
    
    // Apply camera and zoom
    cairo_save(cr);
    cairo_translate(cr, camera_x, camera_y);
    cairo_scale(cr, zoom, zoom);
    
    // Draw background
    if (background_surface) {
        cairo_set_source_surface(cr, background_surface, 0, 0);
        cairo_paint(cr);
    }
    
    // Draw all fish
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (!f->active) continue;
        if (f->group_id >= group_count) continue;
        
        FishGroup *g = &groups[f->group_id];
        cairo_surface_t *surface = (f->vx < 0) ? g->surface_flipped : g->surface;
        if (!surface) continue;
        
        // Draw shadow
        cairo_save(cr);
        cairo_translate(cr, f->x + 3, f->y + 3);
        cairo_rotate(cr, f->angle);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
        cairo_mask_surface(cr, surface, -f->width/2, -f->height/2);
        cairo_restore(cr);
        
        // Draw fish
        cairo_save(cr);
        cairo_translate(cr, f->x, f->y);
        cairo_rotate(cr, f->angle);
        cairo_set_source_surface(cr, surface, -f->width/2, -f->height/2);
        cairo_paint(cr);
        cairo_restore(cr);
        
        // Leader crown
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
        
        // Predator glow
        if (f->is_predator) {
            cairo_save(cr);
            cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 0.25);
            cairo_arc(cr, f->x, f->y, f->width/2 + 10, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        
        // Fleeing effect
        if (f->fleeing) {
            cairo_save(cr);
            cairo_set_source_rgb(cr, 1.0, 1.0, 0.2);
            cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 14);
            cairo_move_to(cr, f->x + 10, f->y - 10);
            cairo_show_text(cr, "!");
            cairo_move_to(cr, f->x - 5, f->y - 15);
            cairo_show_text(cr, "!");
            cairo_restore(cr);
        }
        
        // Selection highlight
        if (selected_fish_index == i) {
            cairo_save(cr);
            cairo_set_source_rgba(cr, 1.0, 0.8, 0.2, 0.4);
            cairo_arc(cr, f->x, f->y, f->width/2 + 8, 0, 2 * M_PI);
            cairo_fill(cr);
            
            cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
            cairo_rectangle(cr, f->x - 45, f->y - f->height - 20, 90, 20);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_set_font_size(cr, 9);
            cairo_move_to(cr, f->x - 40, f->y - f->height - 8);
            cairo_show_text(cr, f->name);
            cairo_restore(cr);
        }
        
        // Hunger bar
        if (f->hunger > 0.7) {
            cairo_save(cr);
            cairo_set_source_rgb(cr, 1.0, 0.5, 0);
            cairo_rectangle(cr, f->x - 15, f->y - f->height/2 - 5, 30 * f->hunger, 3);
            cairo_fill(cr);
            cairo_restore(cr);
        }
    }
    
    draw_particles(cr);
    cairo_restore(cr);
    
    return FALSE;
}

gboolean on_mouse_move(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    hover_x = (event->x - camera_x) / zoom;
    hover_y = (event->y - camera_y) / zoom;
    hover_active = 1;
    
    if (dragging) {
        camera_x = event->x - drag_start_x;
        camera_y = event->y - drag_start_y;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
    
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        double dx = f->x - hover_x;
        double dy = f->y - hover_y;
        double dist = sqrt(dx*dx + dy*dy);
        
        if (dist < 50) {
            if (selected_fish_index != i) {
                selected_fish_index = i;
                update_stats();
            }
            flee_from_hover(i);
            break;
        } else if (selected_fish_index == i) {
            selected_fish_index = -1;
            update_stats();
        }
    }
    
    return FALSE;
}

gboolean on_mouse_button(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (event->button == 1) {
        dragging = 1;
        drag_start_x = event->x - camera_x;
        drag_start_y = event->y - camera_y;
    } else if (event->button == 3) {
        double x = (event->x - camera_x) / zoom;
        double y = (event->y - camera_y) / zoom;
        
        for (int i = 0; i < 30; i++) {
            add_particle(x + (rand() % 60) - 30, y + (rand() % 60) - 30, 1);
        }
        
        for (int i = 0; i < fish_count; i++) {
            double dx = fishes[i].x - x;
            double dy = fishes[i].y - y;
            if (sqrt(dx*dx + dy*dy) < 100) {
                fishes[i].hunger = 0.1;
                fishes[i].energy = 1.0;
                add_particle(fishes[i].x, fishes[i].y, 1);
            }
        }
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

gboolean on_mouse_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    dragging = 0;
    return TRUE;
}

gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    if (event->direction == GDK_SCROLL_UP) {
        zoom *= 1.1;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        zoom /= 1.1;
    }
    if (zoom < 0.5) zoom = 0.5;
    if (zoom > 3.0) zoom = 3.0;
    gtk_widget_queue_draw(widget);
    return TRUE;
}

gboolean animation_tick(gpointer data) {
    update_fish_movement();
    update_particles();
    
    if (drawing_area) {
        gtk_widget_queue_draw(drawing_area);
    }
    return TRUE;
}

void toggle_fullscreen() {
    GtkWindow *window = GTK_WINDOW(main_window_widget);
    
    if (!is_fullscreen) {
        gtk_window_fullscreen(window);
        is_fullscreen = 1;
    } else {
        gtk_window_unfullscreen(window);
        is_fullscreen = 0;
    }
    
    // Force a resize event to update background
    GdkEventConfigure *event = gdk_event_configure_new();
    event->width = is_fullscreen ? gdk_screen_get_width(gdk_screen_get_default()) : DEFAULT_WIDTH;
    event->height = is_fullscreen ? gdk_screen_get_height(gdk_screen_get_default()) : DEFAULT_HEIGHT;
    on_window_resize(drawing_area, event, NULL);
    gdk_event_free((GdkEvent*)event);
}

void update_stats() {
    if (!stats_label) return;
    
    char stats[512];
    int predator_count = 0;
    int normal_count = 0;
    int fleeing_count = 0;
    
    for (int i = 0; i < fish_count; i++) {
        if (fishes[i].is_predator) predator_count++;
        else normal_count++;
        if (fishes[i].fleeing) fleeing_count++;
    }
    
    snprintf(stats, sizeof(stats),
             "<span size='large'><b>🐠 AQUARIUM</b></span>\n\n"
             "🐟 <b>Total:</b> %d\n"
             "🦈 <b>Predators:</b> %d\n"
             "🐠 <b>Normal:</b> %d\n"
             "💨 <b>Fleeing:</b> %d\n"
             "👥 <b>Groups:</b> %d\n"
             "📐 <b>Size:</b> %dx%d\n\n"
             "✨ <b>Selected:</b> %s\n"
             "⚔️ <b>Type:</b> %s\n"
             "💪 <b>Aggression:</b> %d\n\n"
             "━━━━━━━━━━━━━━━━━━━━━\n"
             "💡 <b>Controls:</b>\n"
             "• Click group = Add fish\n"
             "• Hover mouse = Fish flee!\n"
             "• Right-click = Feed area\n"
             "• Drag = Move camera\n"
             "• Scroll = Zoom\n"
             "• F11 or button = Fullscreen",
             fish_count, predator_count, normal_count, fleeing_count, group_count,
             current_width, current_height,
             (selected_fish_index >= 0) ? fishes[selected_fish_index].name : "None",
             (selected_fish_index >= 0) ? (fishes[selected_fish_index].is_predator ? "🦈 Predator" : "🐟 Normal") : "None",
             (selected_fish_index >= 0) ? fishes[selected_fish_index].aggression_level : 0);
    
    gtk_label_set_markup(GTK_LABEL(stats_label), stats);
}

void refresh_groups_list() {
    if (groups_list_box) {
        clear_box(groups_list_box);
    }
    
    for (int i = 0; i < group_count; i++) {
        char button_text[256];
        const char *icon = groups[i].is_predator_group ? "🦈" : "🐟";
        snprintf(button_text, sizeof(button_text), "%s %s (%.1f)", 
                 icon, groups[i].group_name, groups[i].speed);
        
        button *group_btn = create_button();
        group_btn->label = g_strdup(button_text);
        group_btn->bg_color = groups[i].is_predator_group ? g_strdup("#e74c3c") : g_strdup("#2ecc71");
        group_btn->click_callback = on_add_fish;
        group_btn->callback_data = GINT_TO_POINTER(i);
        update_button(group_btn);
        add_to_box(groups_list_box, group_btn->button);
    }
}

void on_add_fish(GtkWidget *widget, gpointer data) {
    int group_id = GPOINTER_TO_INT(data);
    if (group_id >= 0 && group_id < group_count) {
        add_fish(group_id);
        update_stats();
    }
}

void on_feed_fish(GtkWidget *widget, gpointer data) {
    for (int i = 0; i < fish_count; i++) {
        fishes[i].hunger = 0.1;
        fishes[i].energy = 1.0;
        for (int j = 0; j < 3; j++) {
            add_particle(fishes[i].x, fishes[i].y, 1);
        }
    }
    update_stats();
}

void on_make_predator(GtkWidget *widget, gpointer data) {
    if (selected_fish_index >= 0 && selected_fish_index < fish_count) {
        fishes[selected_fish_index].is_predator = 1;
        fishes[selected_fish_index].aggression_level = 5;
        update_stats();
        for (int i = 0; i < 5; i++) {
            add_particle(fishes[selected_fish_index].x, fishes[selected_fish_index].y, 1);
        }
    }
}

void on_make_normal(GtkWidget *widget, gpointer data) {
    if (selected_fish_index >= 0 && selected_fish_index < fish_count) {
        fishes[selected_fish_index].is_predator = 0;
        fishes[selected_fish_index].aggression_level = 2;
        update_stats();
        for (int i = 0; i < 5; i++) {
            add_particle(fishes[selected_fish_index].x, fishes[selected_fish_index].y, 1);
        }
    }
}

void on_add_normal_group(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Fish Image",
                                                     GTK_WINDOW(main_window_widget),
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
        
        GtkWidget *name_dialog = gtk_dialog_new_with_buttons("Group Name",
                                                              GTK_WINDOW(main_window_widget),
                                                              GTK_DIALOG_MODAL,
                                                              "_OK", GTK_RESPONSE_OK,
                                                              NULL);
        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(name_dialog));
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter group name");
        
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Group Name:"), FALSE, FALSE, 5);
        gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 5);
        gtk_container_add(GTK_CONTAINER(content), vbox);
        gtk_widget_show_all(content);
        
        if (gtk_dialog_run(GTK_DIALOG(name_dialog)) == GTK_RESPONSE_OK) {
            const char *group_name = gtk_entry_get_text(GTK_ENTRY(entry));
            if (strlen(group_name) == 0) group_name = "Fish";
            
            GtkWidget *speed_dialog = gtk_dialog_new_with_buttons("Speed",
                                                                  GTK_WINDOW(main_window_widget),
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

void on_add_predator_group(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Predator Image",
                                                     GTK_WINDOW(main_window_widget),
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
        
        GtkWidget *name_dialog = gtk_dialog_new_with_buttons("Predator Group Name",
                                                              GTK_WINDOW(main_window_widget),
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
            
            GtkWidget *speed_dialog = gtk_dialog_new_with_buttons("Predator Speed",
                                                                  GTK_WINDOW(main_window_widget),
                                                                  GTK_DIALOG_MODAL,
                                                                  "_OK", GTK_RESPONSE_OK,
                                                                  NULL);
            GtkWidget *speed_content = gtk_dialog_get_content_area(GTK_DIALOG(speed_dialog));
            GtkWidget *spin = gtk_spin_button_new_with_range(1.0, 8.0, 0.5);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 3.0);
            
            GtkWidget *speed_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
            gtk_box_pack_start(GTK_BOX(speed_vbox), gtk_label_new("Speed:"), FALSE, FALSE, 5);
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

void on_delete_fish(GtkWidget *widget, gpointer data) {
    if (fish_count > 0) {
        delete_fish(fish_count - 1);
    }
}

void on_window_resize(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    current_width = event->width;
    current_height = event->height;
    load_background();
    update_stats();
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    
    ProgramStart();
    
    // Create main window
    window *win = create_window();
    win->title = "🐠 AQUARIUM STUDIO - Dynamic Fullscreen Support";
    win->width = DEFAULT_WIDTH;
    win->height = DEFAULT_HEIGHT;
    win->resizable = TRUE;
    update_window(win);
    
    main_window_widget = win->win;
    
    // Connect resize signal
    g_signal_connect(main_window_widget, "configure-event", G_CALLBACK(on_window_resize), NULL);
    
    // Create main layout
    main_hbox = create_hbox(0, FALSE);
    
    // Create sidebar
    sidebar = create_vbox(10, FALSE);
    sidebar->width = 280;
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
    
    // Stats display
    stats_label = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(stats_label), "<span size='large'>Loading...</span>");
    gtk_widget_set_halign(stats_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(stats_label), TRUE);
    add_to_box(sidebar, stats_label);
    
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    add_to_box(sidebar, sep);
    
    // Group management
    button *create_normal_btn = create_button();
    create_normal_btn->label = "🐟 CREATE NORMAL FISH GROUP";
    create_normal_btn->bg_color = g_strdup("#2ecc71");
    create_normal_btn->click_callback = on_add_normal_group;
    update_button(create_normal_btn);
    add_to_box(sidebar, create_normal_btn->button);
    
    button *create_predator_btn = create_button();
    create_predator_btn->label = "🦈 CREATE PREDATOR GROUP";
    create_predator_btn->bg_color = g_strdup("#e74c3c");
    create_predator_btn->click_callback = on_add_predator_group;
    update_button(create_predator_btn);
    add_to_box(sidebar, create_predator_btn->button);
    
    // Fish management
    button *feed_btn = create_button();
    feed_btn->label = "🍕 FEED ALL FISH";
    feed_btn->bg_color = g_strdup("#f39c12");
    feed_btn->click_callback = on_feed_fish;
    update_button(feed_btn);
    add_to_box(sidebar, feed_btn->button);
    
    button *delete_btn = create_button();
    delete_btn->label = "🗑️ DELETE LAST FISH";
    delete_btn->bg_color = g_strdup("#e67e22");
    delete_btn->click_callback = on_delete_fish;
    update_button(delete_btn);
    add_to_box(sidebar, delete_btn->button);
    
    button *fullscreen_btn = create_button();
    fullscreen_btn->label = "🖥️ TOGGLE FULLSCREEN (F11)";
    fullscreen_btn->bg_color = g_strdup("#9b59b6");
    fullscreen_btn->click_callback = toggle_fullscreen;
    update_button(fullscreen_btn);
    add_to_box(sidebar, fullscreen_btn->button);
    
    sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    add_to_box(sidebar, sep);
    
    // Selected fish actions
    label *selected_label = create_label();
    selected_label->text = "⚙️ SELECTED FISH ACTIONS:";
    selected_label->color = g_strdup("#FFD700");
    selected_label->style = g_strdup("bold");
    update_label(selected_label);
    add_to_box(sidebar, selected_label->label);
    
    button *make_predator_btn = create_button();
    make_predator_btn->label = "🦈 MAKE PREDATOR";
    make_predator_btn->bg_color = g_strdup("#c0392b");
    make_predator_btn->click_callback = on_make_predator;
    update_button(make_predator_btn);
    add_to_box(sidebar, make_predator_btn->button);
    
    button *make_normal_btn = create_button();
    make_normal_btn->label = "🐟 MAKE NORMAL";
    make_normal_btn->bg_color = g_strdup("#27ae60");
    make_normal_btn->click_callback = on_make_normal;
    update_button(make_normal_btn);
    add_to_box(sidebar, make_normal_btn->button);
    
    sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    add_to_box(sidebar, sep);
    
    // Groups list
    label *groups_title = create_label();
    groups_title->text = "📋 YOUR FISH GROUPS:";
    groups_title->color = g_strdup("#87CEEB");
    groups_title->style = g_strdup("bold");
    update_label(groups_title);
    add_to_box(sidebar, groups_title->label);
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 250);
    
    groups_list_box = create_vbox(5, FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled), get_box_widget(groups_list_box));
    add_to_box(sidebar, scrolled);
    
    // Tips
    label *tips_label = create_label();
    tips_label->text = "💡 HOW TO PLAY:\n"
                       "• Click group to add fish\n"
                       "• MOVE MOUSE over fish → They FLEE!\n"
                       "• They will REGROUP after 2 seconds\n"
                       "• Right-click to feed fish\n"
                       "• Drag to move camera\n"
                       "• Scroll to zoom\n"
                       "• Select fish then change type\n"
                       "• Fullscreen is fully dynamic!";
    tips_label->color = g_strdup("#aaa");
    tips_label->style = g_strdup("italic");
    update_label(tips_label);
    add_to_box(sidebar, tips_label->label);
    
    // Spacer
    box *spacer = create_vbox(0, FALSE);
    spacer->expand = TRUE;
    update_box(spacer);
    add_to_box(sidebar, get_box_widget(spacer));
    
    // Drawing area
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, DEFAULT_WIDTH - 280, DEFAULT_HEIGHT);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_mouse_move), NULL);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_mouse_button), NULL);
    g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(on_mouse_release), NULL);
    g_signal_connect(drawing_area, "scroll-event", G_CALLBACK(on_scroll), NULL);
    gtk_widget_add_events(drawing_area, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | 
                          GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK);
    
    // Assemble UI
    add_to_box(main_hbox, get_box_widget(sidebar));
    add_to_box(main_hbox, drawing_area);
    add_to_window(win, get_box_widget(main_hbox));
    
    // Initialize
    current_width = DEFAULT_WIDTH;
    current_height = DEFAULT_HEIGHT;
    load_background();
    
    // Start animation
    animation_timeout_id = g_timeout_add(TIMEOUT_MS, animation_tick, NULL);
    
    display_window(win);
    MainStart();
    
    // Cleanup
    if (animation_timeout_id) g_source_remove(animation_timeout_id);
    for (int i = 0; i < group_count; i++) {
        if (groups[i].surface) cairo_surface_destroy(groups[i].surface);
        if (groups[i].surface_flipped) cairo_surface_destroy(groups[i].surface_flipped);
        g_free(groups[i].image_path);
        g_free(groups[i].group_name);
    }
    if (background_surface) cairo_surface_destroy(background_surface);
    
    ProgramEnd();
    return 0;
}