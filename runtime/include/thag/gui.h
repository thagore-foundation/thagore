#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void* thag_gui_create_canvas(int width, int height, const char* title);
int thag_gui_destroy_canvas(void* canvas);
int thag_gui_clear(void* canvas, int rgba);
int thag_gui_draw_point(void* canvas, int x, int y, int rgba);
int thag_gui_draw_line(void* canvas, int x0, int y0, int x1, int y1, int rgba);
int thag_gui_present(void* canvas);
const char* thag_gui_last_frame_path(void* canvas);
int thag_gui_poll_event(void* canvas);
int thag_gui_should_close(void* canvas);
int thag_gui_request_close(void* canvas);
int thag_gui_set_target_fps(void* canvas, int fps);
int thag_gui_tick(void* canvas);

#ifdef __cplusplus
}
#endif
