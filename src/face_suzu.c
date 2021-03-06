#include <pebble.h>

#define COLORS       true
#define ANTIALIASING true

#define HAND_MARGIN  10
#define FINAL_RADIUS 55

#define ANIMATION_DURATION 500
#define ANIMATION_DELAY    600

typedef struct {
  int hours;
  int minutes;
} Time;

static Window *s_main_window;
static Layer *s_canvas_layer, *s_date_layer;
static TextLayer *s_time_layer,*s_output_layer, *s_output_layer2;

// [date]
static TextLayer *s_day_label, *s_num_label;
static TextLayer *s_day_label2, *s_num_label2;

static BitmapLayer *s_background_layer;
static GBitmap *s_background_bitmap;

// For Date Display
static GFont s_time_font, s_date_font;

static GPoint s_center;
static Time s_last_time, s_anim_time;
static int s_radius = 0, s_anim_hours_60 = 0, s_color_channels[3];
static bool s_animating = false;
// [date]
static char s_num_buffer[4], s_day_buffer[6];

/*************************** AnimationImplementation **************************/

static void animation_started(Animation *anim, void *context) {
  s_animating = true;
}

static void animation_stopped(Animation *anim, bool stopped, void *context) {
  s_animating = false;
}

static void animate(int duration, int delay, AnimationImplementation *implementation, bool handlers) {
  Animation *anim = animation_create();
  animation_set_duration(anim, duration);
  animation_set_delay(anim, delay);
  animation_set_curve(anim, AnimationCurveEaseInOut);
  animation_set_implementation(anim, implementation);
  if(handlers) {
    animation_set_handlers(anim, (AnimationHandlers) {
      .started = animation_started,
      .stopped = animation_stopped
    }, NULL);
  }
  animation_schedule(anim);
}

/************************************ UI **************************************/

// [battery]
static void battery_handler(BatteryChargeState new_state) {
  static char battery_text[] = "<\U0001F638";

  if (new_state.is_charging) {
    snprintf(battery_text, sizeof(battery_text), "<\U0001F63B");
  } else {
    snprintf(battery_text, sizeof(battery_text), "<%d", new_state.charge_percent);
  }
  text_layer_set_text(s_output_layer, battery_text);
  text_layer_set_text(s_output_layer2, battery_text);
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  // Store time
  s_last_time.hours = tick_time->tm_hour;
  s_last_time.hours -= (s_last_time.hours > 12) ? 12 : 0;
  s_last_time.minutes = tick_time->tm_min;

  for(int i = 0; i < 3; i++) {
    // default256->80 changed
    s_color_channels[i] = rand() % 80;
  }

  // Redraw
  if(s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}

static int hours_to_minutes(int hours_out_of_12) {
  return (int)(float)(((float)hours_out_of_12 / 12.0F) * 60.0F);
}

// [date]
static void date_update_proc(Layer *layer, GContext *ctx) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  strftime(s_day_buffer, sizeof(s_day_buffer), "%a", t);
  text_layer_set_text(s_day_label, s_day_buffer);

  strftime(s_num_buffer, sizeof(s_num_buffer), "%d", t);
  text_layer_set_text(s_num_label, s_num_buffer);
}

static void update_proc(Layer *layer, GContext *ctx) {
  // Color background?
  GRect bounds = layer_get_bounds(layer);
  if(COLORS) {
    // TODO CREATE THE BG ROUND ANIMATION WITH OUT MASK CAT 
    //graphics_context_set_fill_color(ctx, GColorFromRGB(s_color_channels[0], s_color_channels[1], s_color_channels[2]));
    //graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  }

  graphics_context_set_stroke_color(ctx, GColorFromRGB(s_color_channels[0], s_color_channels[1], s_color_channels[2]));
  graphics_context_set_stroke_width(ctx, 6);

  graphics_context_set_antialiased(ctx, ANTIALIASING);

  // White clockface
  //graphics_context_set_fill_color(ctx, GColorWhite);
  //graphics_fill_circle(ctx, s_center, s_radius);

  // Draw outline
  // graphics_draw_circle(ctx, s_center, s_radius);

  // Don't use current time while animating
  Time mode_time = (s_animating) ? s_anim_time : s_last_time;

  // Adjust for minutes through the hour
  float minute_angle = TRIG_MAX_ANGLE * mode_time.minutes / 60;
  float hour_angle;
  if(s_animating) {
    // Hours out of 60 for smoothness
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 60;
  } else {
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 12;
  }
  hour_angle += (minute_angle / TRIG_MAX_ANGLE) * (TRIG_MAX_ANGLE / 12);

  // Plot hands
  GPoint minute_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(s_radius - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(TRIG_MAX_ANGLE * mode_time.minutes / 60) * (int32_t)(s_radius - HAND_MARGIN) / TRIG_MAX_RATIO) + s_center.y,
  };
  GPoint hour_hand = (GPoint) {
    .x = (int16_t)(sin_lookup(hour_angle) * (int32_t)(s_radius - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.x,
    .y = (int16_t)(-cos_lookup(hour_angle) * (int32_t)(s_radius - (2 * HAND_MARGIN)) / TRIG_MAX_RATIO) + s_center.y,
  };

  // Draw hands with positive length only
  if(s_radius > 2 * HAND_MARGIN) {
    graphics_draw_line(ctx, s_center, hour_hand);
  } 
  if(s_radius > HAND_MARGIN) {
    graphics_draw_line(ctx, s_center, minute_hand);
  }

}

static void window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);

  // Create GBitmap
  s_background_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BG_SUZU001);

  // Create BitmapLayer to display the GBitmap
  s_background_layer = bitmap_layer_create(window_bounds);
  
  // Set the bitmap onto the layer and add to the window
  bitmap_layer_set_bitmap(s_background_layer, s_background_bitmap);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_background_layer));

  s_center = grect_center_point(&window_bounds);

  s_canvas_layer = layer_create(window_bounds);
  layer_set_update_proc(s_canvas_layer, update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  // [battery] Create output TextLayer
  s_output_layer = text_layer_create(GRect(95, 111, window_bounds.size.w/2, window_bounds.size.h/6));
  text_layer_set_text_color(s_output_layer, GColorOxfordBlue);
  text_layer_set_font(s_output_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_background_color(s_output_layer, GColorClear);
  text_layer_set_text_alignment(s_output_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_output_layer));

  // [battery] Create output2 TextLayer
  s_output_layer2 = text_layer_create(GRect(94, 110, window_bounds.size.w/2, window_bounds.size.h/6));
  text_layer_set_text_color(s_output_layer2, GColorYellow);
  text_layer_set_font(s_output_layer2, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_background_color(s_output_layer2, GColorClear);
  text_layer_set_text_alignment(s_output_layer2, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_output_layer2));

  // [battery] Get the current battery level
  battery_handler(battery_state_service_peek());

  // [date]
  s_date_layer = layer_create(window_bounds);
  layer_set_update_proc(s_date_layer, date_update_proc);
  layer_add_child(window_layer, s_date_layer);
  
  // [date-shadow]
  s_day_label2 = text_layer_create(PBL_IF_ROUND_ELSE(
    //GRect(63, 114, 27, 20),
    GRect(24, 121, 32, 35),
    GRect(46, 114, 27, 20)));
  text_layer_set_text(s_day_label2, s_day_buffer);
  text_layer_set_background_color(s_day_label2, GColorOxfordBlue);
  text_layer_set_text_color(s_day_label2, GColorWhite);
  text_layer_set_font(s_day_label2, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(s_date_layer, text_layer_get_layer(s_day_label2));

  s_day_label = text_layer_create(PBL_IF_ROUND_ELSE(
    //GRect(63, 114, 27, 20),
    GRect(23, 120, 32, 35),
    GRect(46, 114, 27, 20)));
  text_layer_set_text(s_day_label, s_day_buffer);
  text_layer_set_background_color(s_day_label, GColorClear);
  text_layer_set_text_color(s_day_label, GColorYellow);
  text_layer_set_font(s_day_label, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(s_date_layer, text_layer_get_layer(s_day_label));

  // [aaa-shadow]
  s_num_label2 = text_layer_create(PBL_IF_ROUND_ELSE(
    //GRect(90, 114, 18, 20),
    GRect(51, 121, 32, 35),
    GRect(73, 114, 18, 20)));
  text_layer_set_text(s_num_label2, s_num_buffer);
  text_layer_set_background_color(s_num_label2, GColorOxfordBlue);
  text_layer_set_text_color(s_num_label2, GColorWhite);
  text_layer_set_font(s_num_label2, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(s_date_layer, text_layer_get_layer(s_num_label2));

  s_num_label = text_layer_create(PBL_IF_ROUND_ELSE(
    //GRect(90, 114, 18, 20),
    GRect(50, 120, 32, 35),
    GRect(73, 114, 18, 20)));
  text_layer_set_text(s_num_label, s_num_buffer);
  text_layer_set_background_color(s_num_label, GColorClear);
  text_layer_set_text_color(s_num_label, GColorYellow);
  text_layer_set_font(s_num_label, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  layer_add_child(s_date_layer, text_layer_get_layer(s_num_label));

}

static void window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
  layer_destroy(s_date_layer);
}

/*********************************** App **************************************/

static int anim_percentage(AnimationProgress dist_normalized, int max) {
  return (int)(float)(((float)dist_normalized / (float)ANIMATION_NORMALIZED_MAX) * (float)max);
}

static void radius_update(Animation *anim, AnimationProgress dist_normalized) {
  s_radius = anim_percentage(dist_normalized, FINAL_RADIUS);

  layer_mark_dirty(s_canvas_layer);
}

static void hands_update(Animation *anim, AnimationProgress dist_normalized) {
  s_anim_time.hours = anim_percentage(dist_normalized, hours_to_minutes(s_last_time.hours));
  s_anim_time.minutes = anim_percentage(dist_normalized, s_last_time.minutes);

  layer_mark_dirty(s_canvas_layer);
}

static void init() {
  srand(time(NULL));

  time_t t = time(NULL);
  struct tm *time_now = localtime(&t);
  tick_handler(time_now, MINUTE_UNIT);

  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set the background color
  window_set_background_color(s_main_window, GColorBlack);

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  // [battery]Subscribe to the Battery State Service
  battery_state_service_subscribe(battery_handler);

  // Prepare animations
  AnimationImplementation radius_impl = {
    .update = radius_update
  };
  animate(ANIMATION_DURATION, ANIMATION_DELAY, &radius_impl, false);

  AnimationImplementation hands_impl = {
    .update = hands_update
  };
  animate(2 * ANIMATION_DURATION, ANIMATION_DELAY, &hands_impl, true);

}

static void deinit() {
  window_destroy(s_main_window);
}

int main() {
  init();
  app_event_loop();
  deinit();
}
