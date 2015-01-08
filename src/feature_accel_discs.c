#include "pebble.h"
  
#define ACCEL_STEP_MS 20
#define FRAMERATE 100 //i.e. 1000/10 or 10FPS
#define PUNCH 0
#define DUCK 1
#define BLOCK 2
#define DONE 3
#define NONE 4
#define SCORE_KEY 10
#define CALIBRATION_KEY 11
  
//User Interface
static Window *window;
static GRect window_frame;
static Layer *gfx;

//Graphics
static GFont hud_font;
static GFont action_font;
static GBitmap *hit_bmp;
static GBitmap *block_bmp;
static GBitmap *duck_bmp;
char hud_buffer[100];
char action_buffer[10];
char hiscore_buffer[100];

//Game state
static AppTimer *timer;
short current_action;
short interval;
bool fulfilled = false;
short health;
int points;
int hiscore;
bool game_running = false;
unsigned long long start_time;

//Calibration data 
typedef struct CalibrationState { 
  int x;
  int y;
  int z;
} CalibrationState;

typedef struct ActionCalibration {
  CalibrationState punchCal;
  CalibrationState duckCal;
  CalibrationState blockCal;
} ActionCalibration;
ActionCalibration ac;
bool is_calibrating = false;
CalibrationState cal_data;

static void draw(void *data);
static void gfx_update_callback(Layer *me, GContext *ctx);
static void next_iteration();
static void start_game(void* data);
static void end_game();
static void calibrate();
static void action_hint();
static unsigned long long time_in_ms();
static void timer_callback(void *data);
static void up_click_handler(ClickRecognizerRef recognizer, void *context);
static void down_click_handler(ClickRecognizerRef recognizer, void *context);

static void draw(void *data){
  layer_mark_dirty(gfx);
  app_timer_register(FRAMERATE, draw, NULL);
}

//Layer to draw a bitmap representing current state
static void gfx_update_callback(Layer *me, GContext *ctx) {  
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  
  //Set action text
  char* action_name;
  if(current_action == PUNCH){
    action_name = "PUNCH!";
  } else if(current_action == DUCK){
    action_name = "DUCK!";
  } else if(current_action == BLOCK){
    action_name = "BLOCK!";
  } else if(current_action == NONE){
    action_name = "WAIT.";
  } else {
    action_name = "DONE.";
  }
  snprintf(action_buffer, 10, "%s", action_name);
  
  //Draw action
  graphics_draw_text(ctx, action_buffer, action_font, GRect(2, 22, 140, 30), 
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  
  //Draw representational bitmap
  if(current_action == PUNCH){
    graphics_draw_bitmap_in_rect(ctx, hit_bmp, GRect(0, 54, 144, 114));
  } else if(current_action == DUCK){
    graphics_draw_bitmap_in_rect(ctx, duck_bmp, GRect(0, 54, 144, 114));
  } else if(current_action == BLOCK){
    graphics_draw_bitmap_in_rect(ctx, block_bmp, GRect(0, 54, 144, 114));
  } else {
    //Draw a hiscore text
    snprintf(hiscore_buffer, 40, "Top %d", hiscore);
    graphics_draw_text(ctx, hiscore_buffer, hud_font, GRect(2, 54, 140, 18), 
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
  
  //Skip HUD text if calibrating
  if(is_calibrating){
    return;
  }
  
  //Set HUD text
  if(current_action == NONE){
    snprintf(hud_buffer, 40, "By IniPage Software");
        graphics_draw_text(ctx, "Press up to play or down to calibrate", hud_font, GRect(2, 74, 140, 48), 
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  } else if (current_action == DONE){
    snprintf(hud_buffer, 30, "Final Score %d", points);
    graphics_draw_text(ctx, "Press up to play again", hud_font, GRect(2, 74, 140, 36), 
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  } else {
    snprintf(hud_buffer, 40, "HP %d/PTS %d", health, points);
  }
  
  //Draw HUD
  graphics_draw_text(ctx, hud_buffer, hud_font, GRect(2, 2, 140, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void action_hint(){
  switch(current_action){
    case PUNCH:
      vibes_long_pulse();
      break;
    case DUCK:
      vibes_double_pulse();
      break;
    case BLOCK:
      vibes_short_pulse();
      break;
    default:
      break;
  }
}

static void next_iteration(void* data){  
  //Has task been completed?
  if(fulfilled){
    points += 10;
  } else {
    health--;
    if(health < 1){
      end_game();
      return;
    }
  }

  //Set time to perform action down by 10ms
  if(interval > 1500) interval -= 10;
  
  //Set a random action
  fulfilled = false;
  current_action = rand() % 3;  
  action_hint();
  app_timer_register(interval, next_iteration, NULL);
}

static void start_game(void* data){
  health = 3;
  points = 0;
  current_action = NONE;
  fulfilled = false;
  interval = 2500;
  game_running = true;
  //Set up first round
  current_action = rand() % 3;
  action_hint();
  timer = app_timer_register(interval, next_iteration, NULL);
}

static void end_game(){
  if(points > hiscore){
    hiscore = points;
  }
  
  //Does nothing for 3 seconds
  current_action = DONE;
  game_running = false;

  APP_LOG(APP_LOG_LEVEL_DEBUG, "End game called");
  app_timer_cancel(timer);
}


static unsigned long long time_in_ms(){
  return time(NULL) * 1000 + time_ms(NULL, NULL);
}

static void timer_callback(void *data) {
  //Check calibration first
  if(is_calibrating){
    AccelData accel = (AccelData) { .x = 0, .y = 0, .z = 0 };
    accel_service_peek(&accel);
    
    if(abs(accel.x) > abs(cal_data.x)) cal_data.x = accel.x;
    if(abs(accel.y) > abs(cal_data.y)) cal_data.y = accel.y;
    if(abs(accel.z) > abs(cal_data.z)) cal_data.z = accel.y;
    return;
  }
  
  //Update fulfilled if needed
  if(!fulfilled && game_running){
    AccelData accel = (AccelData) { .x = 0, .y = 0, .z = 0 };
    accel_service_peek(&accel);
    
    //Check action, and update fulfilled if needed 
    if(current_action == PUNCH && accel.did_vibrate == false){
      if(accel.x > 1500) fulfilled = true;
    } else if(current_action == DUCK && accel.did_vibrate == false){
      if(accel.z < -1500) fulfilled = true;
    } else if(current_action == BLOCK && accel.did_vibrate == false){
      if(accel.y > 1500) fulfilled = true;
    }
  }
  
  app_timer_register(ACCEL_STEP_MS, timer_callback, NULL);
}

static void calibrate(){
  is_calibrating = true;
}

//Indicates up click
static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if(!game_running) start_game(NULL);
}

//Indicates down click
static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if(!game_running && !is_calibrating) calibrate();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect frame = window_frame = layer_get_frame(window_layer);
  gfx = layer_create(frame);
  layer_set_update_proc(gfx, gfx_update_callback);
  layer_add_child(window_layer, gfx);
  
  window_set_click_config_provider(window, click_config_provider);
  
  current_action = NONE;
  app_timer_register(FRAMERATE, draw, NULL); //Start draw loop
  app_timer_register(25, timer_callback, NULL); //Start accelerometer data collection
}

static void window_unload(Window *window) {
  layer_destroy(gfx);
}

static void init(void) {
  srand(time(NULL));
  
  //Get resources
  hud_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  action_font = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
  hit_bmp = gbitmap_create_with_resource(RESOURCE_ID_HIT);
  duck_bmp = gbitmap_create_with_resource(RESOURCE_ID_DUCK);
  block_bmp = gbitmap_create_with_resource(RESOURCE_ID_BLOCK);
  
  start_time = time_in_ms();
  accel_data_service_subscribe(0, NULL);
  
  if(persist_exists(SCORE_KEY)){
    hiscore = persist_read_int(SCORE_KEY);
  } else {
    hiscore = 0;
  }
  
  if(persist_exists(CALIBRATION_KEY)){
    persist_read_data(CALIBRATION_KEY, &ac, sizeof(ac));
  } else {
    ac = (ActionCalibration) { (CalibrationState){1500, 0, 0}, (CalibrationState){0, 0, -1500}, 
      (CalibrationState){0, 1500, 0} };
  }
  
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });
  window_set_fullscreen(window, true);
  window_set_background_color(window, GColorWhite);
  window_stack_push(window, true);
}

static void deinit(void) {
  persist_write_int(SCORE_KEY, hiscore);
  persist_write_data(CALIBRATION_KEY, &ac, sizeof(ac));
  gbitmap_destroy(hit_bmp);
  gbitmap_destroy(block_bmp);
  gbitmap_destroy(duck_bmp);
  accel_data_service_unsubscribe();
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}