#include "structures.h"
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define MAX_FISH 300
#define MAX_GROUPS 30
#define WINDOW_WIDTH 1366
#define WINDOW_HEIGHT 768
#define TIMEOUT_MS 16
#define AVOID_DISTANCE 150
#define REGROUP_DELAY 1500
#define MAX_SPEED_MULTIPLIER 5.0
#define MAX_PARTICLES 500
#define MAX_BUBBLES 100

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
    double target_x, target_y;
    int group_id;
    int is_leader;
    int is_predator;
    int aggression_level;
    int width, height;
    double angle;
    double angle_offset;
    int fleeing;
    guint flee_timeout_id;
    int active;
    double hunger;
    double energy;
    int age;
    double scale;
    char name[50];
} Fish;

typedef struct {
    int group_id;
    char *image_path;
    char *group_name;
    double speed;
    double agility;
    double strength;
    int member_count;
    int members_indices[MAX_FISH];
    GdkPixbuf *pixbuf_cache;
    cairo_surface_t *surface;
    cairo_surface_t *surface_flipped;
    int img_width, img_height;
    int leader_index;
    int is_predator_group;
    char sound_path[256];
    double color_r, color_g, color_b;
} FishGroup;

// Global variables
Fish fishes[MAX_FISH];
FishGroup groups[MAX_GROUPS];
Particle particles[MAX_PARTICLES];
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
cairo_surface_t *background_surface = NULL;
int current_width = WINDOW_WIDTH;
int current_height = WINDOW_HEIGHT;
double time_of_day = 0.5;
int selected_fish_index = -1;
double mouse_press_x = 0, mouse_press_y = 0;
int dragging = 0;
double camera_x = 0, camera_y = 0;
double zoom = 1.0;
GtkWidget *stats_label;

// Function prototypes
void add_fish(int group_id);
void create_fish_group(const char *image_path, const char *group_name, double speed, int is_predator_group);
void update_animation();
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
void draw_bubbles(cairo_t *cr, int width, int height);
void refresh_groups_list();
void on_add_normal_fish(GtkWidget *widget, gpointer data);
void on_add_predator_group(GtkWidget *widget, gpointer data);
void on_add_fish_group(GtkWidget *widget, gpointer data);
void on_delete_fish(GtkWidget *widget, gpointer data);
void on_window_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data);
void add_particle(double x, double y, int type);
void update_particles();
void draw_particles(cairo_t *cr);
void draw_underwater_plants(cairo_t *cr, int width, int height);
void draw_caustics(cairo_t *cr, int width, int height);
void update_stats();
void on_feed_fish(GtkWidget *widget, gpointer data);
void on_clean_aquarium(GtkWidget *widget, gpointer data);
void on_time_changed(GtkRange *range, gpointer data);
gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data);

void add_particle(double x, double y, int type) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].life) {
            particles[i].x = x;
            particles[i].y = y;
            particles[i].vx = ((rand() % 100) - 50) / 20.0;
            particles[i].vy = ((rand() % 100) - 50) / 20.0;
            particles[i].life = 30 + rand() % 30;
            particles[i].max_life = particles[i].life;
            particles[i].type = type;
            particles[i].size = 2 + (rand() % 6);
            
            if (type == 0) {
                particles[i].r = 100 + rand() % 155;
                particles[i].g = 150 + rand() % 105;
                particles[i].b = 255;
                particles[i].vy = -2 - (rand() % 5);
            } else if (type == 1) {
                particles[i].r = 255;
                particles[i].g = 200 + rand() % 55;
                particles[i].b = 100;
            } else {
                particles[i].r = 200;
                particles[i].g = 200;
                particles[i].b = 255;
                particles[i].size = 5;
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
            
            if (particles[i].x < 0) particles[i].x = current_width;
            if (particles[i].x > current_width) particles[i].x = 0;
            if (particles[i].y < 0) particles[i].life = 0;
            if (particles[i].y > current_height) particles[i].life = 0;
        }
    }
}

void draw_particles(cairo_t *cr) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            double alpha = (double)particles[i].life / particles[i].max_life;
            cairo_set_source_rgba(cr, particles[i].r/255.0, particles[i].g/255.0, particles[i].b/255.0, alpha * 0.7);
            
            if (particles[i].type == 2) {
                cairo_set_line_width(cr, 1);
                cairo_arc(cr, particles[i].x, particles[i].y, particles[i].size * (1 - alpha), 0, 2 * M_PI);
                cairo_stroke(cr);
            } else {
                cairo_arc(cr, particles[i].x, particles[i].y, particles[i].size * alpha, 0, 2 * M_PI);
                cairo_fill(cr);
            }
        }
    }
}

void draw_underwater_plants(cairo_t *cr, int width, int height) {
    cairo_save(cr);
    
    for (int i = 0; i < 50; i++) {
        double x = (i * 137) % width;
        double base_y = height - 50;
        double height_plant = 60 + (i % 100);
        
        cairo_pattern_t *grad = cairo_pattern_create_linear(x, base_y, x, base_y - height_plant);
        cairo_pattern_add_color_stop_rgb(grad, 0, 0.1, 0.4, 0.1);
        cairo_pattern_add_color_stop_rgb(grad, 1, 0.2, 0.6, 0.2);
        
        cairo_move_to(cr, x, base_y);
        
        for (double y = 0; y <= height_plant; y += 10) {
            double offset = sin(y * 0.05 + time_of_day * 10) * 10;
            cairo_line_to(cr, x + offset, base_y - y);
        }
        
        cairo_line_to(cr, x, base_y);
        cairo_set_source(cr, grad);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);
        
        cairo_move_to(cr, x, base_y - height_plant/2);
        cairo_line_to(cr, x + 15, base_y - height_plant/2 - 20);
        cairo_line_to(cr, x, base_y - height_plant/2 - 10);
        cairo_set_source_rgba(cr, 0.2, 0.5, 0.2, 0.8);
        cairo_fill(cr);
    }
    
    cairo_restore(cr);
}

void draw_caustics(cairo_t *cr, int width, int height) {
    static int caustic_frame = 0;
    caustic_frame++;
    
    cairo_save(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.8, 0.1);
    
    for (int i = 0; i < 30; i++) {
        double x = (i * 97 + caustic_frame) % width;
        double y = height - 50 + sin(caustic_frame * 0.02 + i) * 10;
        
        cairo_move_to(cr, x, y);
        cairo_line_to(cr, x + 30, y - 20);
        cairo_line_to(cr, x - 30, y - 20);
        cairo_fill(cr);
    }
    
    cairo_restore(cr);
}

void load_background() {
    if (background_surface) {
        cairo_surface_destroy(background_surface);
    }
    
    background_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, current_width, current_height);
    cairo_t *cr = cairo_create(background_surface);
    
    cairo_pattern_t *gradient = cairo_pattern_create_linear(0, 0, 0, current_height);
    
    double r, g, b;
    if (time_of_day < 0.5) {
        r = 0.02 + time_of_day * 0.1;
        g = 0.05 + time_of_day * 0.2;
        b = 0.15 + time_of_day * 0.3;
    } else {
        r = 0.1 + (time_of_day - 0.5) * 0.3;
        g = 0.2 + (time_of_day - 0.5) * 0.2;
        b = 0.4 - (time_of_day - 0.5) * 0.2;
    }
    
    cairo_pattern_add_color_stop_rgb(gradient, 0.0, r, g, b);
    cairo_pattern_add_color_stop_rgb(gradient, 0.4, r*0.8, g*0.9, b*0.9);
    cairo_pattern_add_color_stop_rgb(gradient, 0.7, r*0.6, g*0.7, b*0.8);
    cairo_pattern_add_color_stop_rgb(gradient, 1.0, r*0.4, g*0.5, b*0.7);
    
    cairo_rectangle(cr, 0, 0, current_width, current_height);
    cairo_set_source(cr, gradient);
    cairo_fill(cr);
    cairo_pattern_destroy(gradient);
    
    double sun_y = current_height * 0.2;
    double sun_radius = 50;
    if (time_of_day > 0.3 && time_of_day < 0.7) {
        cairo_set_source_rgba(cr, 1.0, 0.9, 0.5, 0.3);
        cairo_arc(cr, current_width * 0.8, sun_y, sun_radius, 0, 2 * M_PI);
        cairo_fill(cr);
        
        for (int i = 0; i < 12; i++) {
            double angle = i * M_PI * 2 / 12;
            cairo_move_to(cr, current_width * 0.8, sun_y);
            cairo_line_to(cr, current_width * 0.8 + cos(angle) * (sun_radius + 20), 
                              sun_y + sin(angle) * (sun_radius + 20));
            cairo_set_line_width(cr, 3);
            cairo_stroke(cr);
        }
    }
    
    cairo_set_source_rgba(cr, 1.0, 1.0, 0.8, 0.05);
    for (int i = 0; i < 20; i++) {
        double x = (rand() % current_width);
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x + (rand() % 400) - 200, current_height);
        cairo_line_to(cr, x + (rand() % 200) - 100, current_height);
        cairo_fill(cr);
    }
    
    cairo_destroy(cr);
}

void draw_bubbles(cairo_t *cr, int width, int height) {
    static int bubble_frame = 0;
    bubble_frame = (bubble_frame + 1) % 360;
    
    for (int i = 0; i < MAX_BUBBLES; i++) {
        int x = (i * 173 + bubble_frame * 2) % width;
        int y = (height - (i * 37 + bubble_frame * 3) % height);
        int size = 2 + (i % 15);
        
        cairo_set_source_rgba(cr, 0.7, 0.8, 1.0, 0.3);
        cairo_arc(cr, x, y, size, 0, 2 * M_PI);
        cairo_fill(cr);
        
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
        cairo_arc(cr, x - size/3, y - size/3, size/4, 0, 2 * M_PI);
        cairo_fill(cr);
    }
}

cairo_surface_t* load_fish_surface(const char *path, int *out_width, int *out_height, int target_size, int flip) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);
    
    if (!pixbuf) {
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_size, target_size);
        cairo_t *cr = cairo_create(surface);
        
        cairo_pattern_t *grad = cairo_pattern_create_radial(target_size/3, target_size/3, 0,
                                                             target_size/2, target_size/2, target_size/2);
        cairo_pattern_add_color_stop_rgb(grad, 0, 1.0, 0.5, 0.2);
        cairo_pattern_add_color_stop_rgb(grad, 1, 0.8, 0.3, 0.1);
        
        cairo_save(cr);
        cairo_translate(cr, target_size/2, target_size/2);
        cairo_scale(cr, 1.0, 0.6);
        cairo_arc(cr, 0, 0, target_size/2, 0, 2 * M_PI);
        cairo_restore(cr);
        cairo_set_source(cr, grad);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);
        
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_arc(cr, target_size*0.7, target_size*0.4, target_size/8, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_arc(cr, target_size*0.72, target_size*0.38, target_size/12, 0, 2 * M_PI);
        cairo_fill(cr);
        
        cairo_move_to(cr, target_size*0.2, target_size*0.3);
        cairo_line_to(cr, target_size*0.1, target_size*0.5);
        cairo_line_to(cr, target_size*0.2, target_size*0.7);
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

void create_fish_group(const char *image_path, const char *group_name, double speed, int is_predator_group) {
    if (group_count >= MAX_GROUPS) return;
    
    groups[group_count].group_id = group_count;
    groups[group_count].image_path = g_strdup(image_path);
    groups[group_count].group_name = g_strdup(group_name);
    groups[group_count].speed = speed;
    groups[group_count].agility = 1.0 + (rand() % 100) / 100.0;
    groups[group_count].strength = 0.5 + (rand() % 100) / 100.0;
    groups[group_count].member_count = 0;
    groups[group_count].leader_index = -1;
    groups[group_count].is_predator_group = is_predator_group;
    
    groups[group_count].color_r = 0.3 + (rand() % 70) / 100.0;
    groups[group_count].color_g = 0.2 + (rand() % 70) / 100.0;
    groups[group_count].color_b = 0.4 + (rand() % 60) / 100.0;
    
    groups[group_count].surface = load_fish_surface(image_path, 
                                                     &groups[group_count].img_width,
                                                     &groups[group_count].img_height, 55, 0);
    groups[group_count].surface_flipped = load_fish_surface(image_path, 
                                                             &groups[group_count].img_width,
                                                             &groups[group_count].img_height, 55, 1);
    
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
    f->aggression_level = f->is_predator ? 5 + rand() % 5 : 1 + rand() % 3;
    f->is_leader = 0;
    f->fleeing = 0;
    f->flee_timeout_id = 0;
    f->active = 1;
    f->width = groups[group_id].img_width;
    f->height = groups[group_id].img_height;
    f->angle = 0;
    f->angle_offset = (rand() % 360) * M_PI / 180;
    f->hunger = 0.5;
    f->energy = 0.8;
    f->age = 0;
    f->scale = 0.8 + (rand() % 40) / 100.0;
    sprintf(f->name, "%s #%d", groups[group_id].group_name, fish_count + 1);
    
    f->x = (rand() % (current_width - 200)) + 100;
    f->y = (rand() % (current_height - 200)) + 100;
    
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
    
    add_particle(f->x, f->y, 2);
}

void delete_fish(int fish_idx) {
    if (fish_idx < 0 || fish_idx >= fish_count) return;
    
    Fish *f = &fishes[fish_idx];
    if (f->group_id >= group_count) return;
    
    for (int i = 0; i < 15; i++) {
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
        
        f->age++;
        f->hunger += 0.001;
        f->energy -= 0.001;
        
        if (f->fleeing) {
            double dx = f->x - hover_x;
            double dy = f->y - hover_y;
            double dist = sqrt(dx*dx + dy*dy);
            if (dist > 0.01 && dist < AVOID_DISTANCE * 2) {
                double angle = atan2(dy, dx);
                f->vx = cos(angle) * g->speed * MAX_SPEED_MULTIPLIER * g->agility;
                f->vy = sin(angle) * g->speed * MAX_SPEED_MULTIPLIER * g->agility;
            }
        } else if (!f->is_leader && g->leader_index >= 0 && g->leader_index < g->member_count) {
            int leader_idx = g->members_indices[g->leader_index];
            if (leader_idx >= 0 && leader_idx < fish_count && fishes[leader_idx].active) {
                Fish *leader = &fishes[leader_idx];
                double dx = leader->x - f->x;
                double dy = leader->y - f->y;
                double dist = sqrt(dx*dx + dy*dy);
                if (dist > 50) {
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
            if (rand() % 100 < 2) {
                double angle = (rand() % 360) * M_PI / 180.0;
                f->vx = cos(angle) * g->speed;
                f->vy = sin(angle) * g->speed;
            }
            f->vx += ((rand() % 100) - 50) / 800.0;
            f->vy += ((rand() % 100) - 50) / 800.0;
        }
        
        f->x += f->vx;
        f->y += f->vy;
        
        int padding = 80;
        if (f->x < -padding) {
            f->x = current_width + padding;
            add_particle(f->x, f->y, 2);
        }
        if (f->x > current_width + padding) {
            f->x = -padding;
            add_particle(f->x, f->y, 2);
        }
        if (f->y < -padding) {
            f->y = current_height + padding;
            add_particle(f->x, f->y, 2);
        }
        if (f->y > current_height + padding) {
            f->y = -padding;
            add_particle(f->x, f->y, 2);
        }
        
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
            add_particle(f->x, f->y, 0);
            
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
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    
    if (allocation.width != current_width || allocation.height != current_height) {
        current_width = allocation.width;
        current_height = allocation.height;
        load_background();
    }
    
    cairo_save(cr);
    cairo_translate(cr, camera_x, camera_y);
    cairo_scale(cr, zoom, zoom);
    
    if (background_surface) {
        cairo_set_source_surface(cr, background_surface, 0, 0);
        cairo_paint(cr);
    }
    
    draw_underwater_plants(cr, current_width, current_height);
    draw_caustics(cr, current_width, current_height);
    draw_bubbles(cr, current_width, current_height);
    
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (!f->active) continue;
        if (f->group_id >= group_count) continue;
        
        FishGroup *g = &groups[f->group_id];
        cairo_surface_t *surface = (f->vx < 0) ? g->surface_flipped : g->surface;
        if (!surface) continue;
        
        cairo_save(cr);
        cairo_translate(cr, f->x, f->y);
        cairo_rotate(cr, f->angle + sin(f->age * 0.05) * 0.1);
        cairo_scale(cr, f->scale, f->scale);
        
        cairo_set_source_surface(cr, surface, -f->width/2, -f->height/2);
        cairo_paint(cr);
        
        cairo_restore(cr);
        
        if (f->is_leader && !f->fleeing) {
            cairo_save(cr);
            cairo_set_source_rgb(cr, 1.0, 0.85, 0.2);
            cairo_arc(cr, f->x, f->y - f->height/2 - 8, 8, 0, 2 * M_PI);
            cairo_fill(cr);
            
            cairo_set_source_rgb(cr, 1.0, 0.6, 0.0);
            cairo_arc(cr, f->x, f->y - f->height/2 - 8, 4, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        
        if (f->is_predator) {
            cairo_save(cr);
            cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 0.25 + sin(f->age * 0.1) * 0.1);
            cairo_arc(cr, f->x, f->y, f->width/2 + 15, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        
        if (selected_fish_index == i) {
            cairo_save(cr);
            cairo_set_source_rgba(cr, 0, 0, 0, 0.6);
            cairo_rectangle(cr, f->x - 40, f->y - f->height - 20, 80, 20);
            cairo_fill(cr);
            
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 10);
            cairo_move_to(cr, f->x - 35, f->y - f->height - 8);
            cairo_show_text(cr, f->name);
            cairo_restore(cr);
        }
        
        if (f->hunger > 0.8) {
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
    hover_x = event->x / zoom - camera_x;
    hover_y = event->y / zoom - camera_y;
    hover_active = 1;
    
    if (dragging) {
        camera_x = event->x - mouse_press_x;
        camera_y = event->y - mouse_press_y;
        return TRUE;
    }
    
    for (int i = 0; i < fish_count; i++) {
        Fish *f = &fishes[i];
        if (!f->active) continue;
        
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
        mouse_press_x = event->x - camera_x;
        mouse_press_y = event->y - camera_y;
    } else if (event->button == 3) {
        double x = event->x / zoom - camera_x;
        double y = event->y / zoom - camera_y;
        for (int i = 0; i < 30; i++) {
            add_particle(x + (rand() % 50) - 25, y + (rand() % 50) - 25, 1);
        }
        for (int i = 0; i < fish_count; i++) {
            double dx = fishes[i].x - x;
            double dy = fishes[i].y - y;
            double dist = sqrt(dx*dx + dy*dy);
            if (dist < 100) {
                fishes[i].hunger = 0.2;
                fishes[i].energy = 1.0;
            }
        }
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
    time_of_day += 0.0005;
    if (time_of_day > 1.0) time_of_day = 0;
    load_background();
    
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

void update_stats() {
    if (!stats_label) return;
    
    char stats[1024];
    int predator_count = 0;
    int normal_count = 0;
    
    for (int i = 0; i < fish_count; i++) {
        if (fishes[i].is_predator) predator_count++;
        else normal_count++;
    }
    
    snprintf(stats, sizeof(stats),
             "<span size='large'><b>🐠 AQUARIUM STATS</b></span>\n\n"
             "<span foreground='#87CEEB'>━━━━━━━━━━━━━━━━━━━━━</span>\n\n"
             "🐟 <b>Total Fish:</b> %d\n"
             "🦈 <b>Predators:</b> %d\n"
             "🐠 <b>Normal Fish:</b> %d\n"
             "👥 <b>Fish Groups:</b> %d\n\n"
             "✨ <b>Selected Fish:</b> %s\n"
             "🎯 <b>Group:</b> %s\n"
             "💪 <b>Aggression:</b> %d\n\n"
             "<span foreground='#FFD700'>━━━━━━━━━━━━━━━━━━━━━</span>\n"
             "💡 <b>Tips:</b>\n"
             "• Hover = Fish flee!\n"
             "• Right-click = Add food\n"
             "• Drag = Move camera\n"
             "• Scroll = Zoom\n"
             "• Click fish = Select",
             fish_count, predator_count, normal_count, group_count,
             (selected_fish_index >= 0) ? fishes[selected_fish_index].name : "None",
             (selected_fish_index >= 0 && selected_fish_index < fish_count) ? 
                 groups[fishes[selected_fish_index].group_id].group_name : "None",
             (selected_fish_index >= 0 && selected_fish_index < fish_count) ? 
                 fishes[selected_fish_index].aggression_level : 0);
    
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
        group_btn->click_callback = on_add_normal_fish;
        group_btn->callback_data = GINT_TO_POINTER(i);
        update_button(group_btn);
        add_to_box(groups_list_box, group_btn->button);
    }
}

void on_add_normal_fish(GtkWidget *widget, gpointer data) {
    int group_id = GPOINTER_TO_INT(data);
    if (group_id >= 0 && group_id < group_count) {
        add_fish(group_id);
        update_stats();
    }
}

void on_feed_fish(GtkWidget *widget, gpointer data) {
    for (int i = 0; i < fish_count; i++) {
        fishes[i].hunger = 0.2;
        fishes[i].energy = 1.0;
        for (int j = 0; j < 5; j++) {
            add_particle(fishes[i].x, fishes[i].y, 1);
        }
    }
    update_stats();
}

void on_clean_aquarium(GtkWidget *widget, gpointer data) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particles[i].life = 0;
    }
    update_stats();
}

void on_time_changed(GtkRange *range, gpointer data) {
    time_of_day = gtk_range_get_value(range);
    load_background();
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
            
            GtkWidget *speed_dialog = gtk_dialog_new_with_buttons("Set Fish Speed",
                                                                  GTK_WINDOW(main_window->win),
                                                                  GTK_DIALOG_MODAL,
                                                                  "_OK", GTK_RESPONSE_OK,
                                                                  NULL);
            GtkWidget *speed_content = gtk_dialog_get_content_area(GTK_DIALOG(speed_dialog));
            GtkWidget *spin = gtk_spin_button_new_with_range(0.5, 8.0, 0.5);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 2.5);
            
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
            
            GtkWidget *speed_dialog = gtk_dialog_new_with_buttons("Set Predator Speed",
                                                                  GTK_WINDOW(main_window->win),
                                                                  GTK_DIALOG_MODAL,
                                                                  "_OK", GTK_RESPONSE_OK,
                                                                  NULL);
            GtkWidget *speed_content = gtk_dialog_get_content_area(GTK_DIALOG(speed_dialog));
            GtkWidget *spin = gtk_spin_button_new_with_range(1.0, 10.0, 0.5);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 3.5);
            
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
    
    main_window = create_window();
    main_window->title = "🐠 AQUARIUM SIMULATOR ULTIMATE";
    main_window->width = WINDOW_WIDTH;
    main_window->height = WINDOW_HEIGHT;
    main_window->resizable = TRUE;
    update_window(main_window);
    
    g_signal_connect(main_window->win, "configure-event", G_CALLBACK(on_window_configure), NULL);
    
    main_hbox = create_hbox(0, FALSE);
    
    sidebar = create_vbox(10, FALSE);
    sidebar->width = 300;
    sidebar->bg_color = g_strdup("rgba(0, 0, 0, 0.9)");
    sidebar->margin_top = sidebar->margin_bottom = sidebar->margin_left = sidebar->margin_right = 10;
    update_box(sidebar);
    
    label *title_label = create_label();
    title_label->text = "🐠 AQUARIUM STUDIO ULTIMATE 🐙";
    title_label->color = g_strdup("#FFD700");
    title_label->style = g_strdup("bold");
    title_label->font_family = g_strdup("Arial Black");
    update_label(title_label);
    add_to_box(sidebar, title_label->label);
    
    stats_label = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(stats_label), "<span size='large'>Loading...</span>");
    gtk_widget_set_halign(stats_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(stats_label), TRUE);
    add_to_box(sidebar, stats_label);
    
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    add_to_box(sidebar, sep1);
    
    button *create_fish_btn = create_button();
    create_fish_btn->label = "🐟 CREATE FISH GROUP";
    create_fish_btn->bg_color = g_strdup("#2ecc71");
    create_fish_btn->click_callback = on_add_fish_group;
    update_button(create_fish_btn);
    add_to_box(sidebar, create_fish_btn->button);
    
    button *create_predator_btn = create_button();
    create_predator_btn->label = "🦈 CREATE PREDATOR GROUP";
    create_predator_btn->bg_color = g_strdup("#e74c3c");
    create_predator_btn->click_callback = on_add_predator_group;
    update_button(create_predator_btn);
    add_to_box(sidebar, create_predator_btn->button);
    
    button *feed_btn = create_button();
    feed_btn->label = "🍕 FEED ALL FISH";
    feed_btn->bg_color = g_strdup("#f39c12");
    feed_btn->click_callback = on_feed_fish;
    update_button(feed_btn);
    add_to_box(sidebar, feed_btn->button);
    
    button *clean_btn = create_button();
    clean_btn->label = "✨ CLEAN AQUARIUM";
    clean_btn->bg_color = g_strdup("#3498db");
    clean_btn->click_callback = on_clean_aquarium;
    update_button(clean_btn);
    add_to_box(sidebar, clean_btn->button);
    
    button *delete_btn = create_button();
    delete_btn->label = "🗑️ DELETE LAST FISH";
    delete_btn->bg_color = g_strdup("#e67e22");
    delete_btn->click_callback = on_delete_fish;
    update_button(delete_btn);
    add_to_box(sidebar, delete_btn->button);
    
    button *fullscreen_btn = create_button();
    fullscreen_btn->label = "🖥️ TOGGLE FULLSCREEN";
    fullscreen_btn->bg_color = g_strdup("#9b59b6");
    fullscreen_btn->click_callback = toggle_fullscreen;
    update_button(fullscreen_btn);
    add_to_box(sidebar, fullscreen_btn->button);
    
    label *time_label = create_label();
    time_label->text = "🌞 TIME OF DAY:";
    time_label->color = g_strdup("#87CEEB");
    update_label(time_label);
    add_to_box(sidebar, time_label->label);
    
    GtkWidget *time_slider_widget = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.01);
    gtk_range_set_value(GTK_RANGE(time_slider_widget), 0.5);
    g_signal_connect(time_slider_widget, "value-changed", G_CALLBACK(on_time_changed), NULL);
    add_to_box(sidebar, time_slider_widget);
    
    label *groups_title = create_label();
    groups_title->text = "📋 YOUR FISH GROUPS:";
    groups_title->color = g_strdup("#FFD700");
    groups_title->style = g_strdup("bold");
    update_label(groups_title);
    add_to_box(sidebar, groups_title->label);
    
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 250);
    
    groups_list_box = create_vbox(5, FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled), get_box_widget(groups_list_box));
    add_to_box(sidebar, scrolled);
    
    label *tips_label = create_label();
    tips_label->text = "💡 PRO TIPS:\n"
                       "• Click group buttons to add fish\n"
                       "• Hover over fish to make them flee!\n"
                       "• Right-click anywhere to add food\n"
                       "• Drag background to move camera\n"
                       "• Scroll to zoom in/out\n"
                       "• Predators scare normal fish\n"
                       "• Feed fish to keep them healthy!";
    tips_label->color = g_strdup("#aaa");
    tips_label->style = g_strdup("italic");
    update_label(tips_label);
    add_to_box(sidebar, tips_label->label);
    
    box *spacer = create_vbox(0, FALSE);
    spacer->expand = TRUE;
    update_box(spacer);
    add_to_box(sidebar, get_box_widget(spacer));
    
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(drawing_area, WINDOW_WIDTH - 300, WINDOW_HEIGHT);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_mouse_move), NULL);
    g_signal_connect(drawing_area, "button-press-event", G_CALLBACK(on_mouse_button), NULL);
    g_signal_connect(drawing_area, "button-release-event", G_CALLBACK(on_mouse_release), NULL);
    g_signal_connect(drawing_area, "scroll-event", G_CALLBACK(on_scroll), NULL);
    gtk_widget_add_events(drawing_area, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK);
    
    add_to_box(main_hbox, get_box_widget(sidebar));
    add_to_box(main_hbox, drawing_area);
    add_to_window(main_window, get_box_widget(main_hbox));
    
    current_width = WINDOW_WIDTH;
    current_height = WINDOW_HEIGHT;
    load_background();
    
    animation_timeout_id = g_timeout_add(TIMEOUT_MS, animation_tick, NULL);
    
    display_window(main_window);
    MainStart();
    
    if (animation_timeout_id) g_source_remove(animation_timeout_id);
    for (int i = 0; i < group_count; i++) {
        if (groups[i].pixbuf_cache) g_object_unref(groups[i].pixbuf_cache);
        if (groups[i].surface) cairo_surface_destroy(groups[i].surface);
        if (groups[i].surface_flipped) cairo_surface_destroy(groups[i].surface_flipped);
        g_free(groups[i].image_path);
        g_free(groups[i].group_name);
    }
    if (background_surface) cairo_surface_destroy(background_surface);
    
    ProgramEnd();
    return 0;
}