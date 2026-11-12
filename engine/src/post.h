#ifndef POST_PROCESS_H
#define POST_PROCESS_H

// Post-processing (an option in the pause menu) and the resolution option. The world is drawn
// into an offscreen target (the resolution setting's size: 480x272 .. 960x544), then scaled
// onto the 960x544 screen - as it is, or with post-processing: soft bloom (bright pass + blur at
// quarter resolution), a desaturated grade with cool shadows and warm highlights, and
// light ordered dithering.

#define POST_W 960 // (full resolution: 640x368 made the picture softer than drawing directly)
#define POST_H 544

// Returns 0 if the shaders or targets couldn't be made (post_begin then does nothing).
int post_init(void);
// On/off (the menu); takes effect at the next post_begin
void post_set_enabled(int on);
enum { POST_UPSCALE_OFF, POST_UPSCALE_BILINEAR }; // (the Upscaler option: Off - the world's pixels scaled up as they are,
// blocks; Bilinear - smoothed between them)
void post_set_upscaler(int kind);
void post_set_motion_blur(int on); // (the option: the view's turns smeared)
void post_camera_motion(float u, float v); // (this frame's turn: how far across / up the screen, its share of it) // (the option: FXAA on the world as drawn, before it's scaled up)
// The world's resolution (the menu: 480x272, 720x408, 864x480, 960x544 - the screen's own:
// drawn straight to it unless post-processing is on); takes effect at the next post_begin
void post_set_resolution(int w, int h);
// The dynamic resolution: this frame's size, within the setting's (main.c chooses it by how the
// Vita's GPU keeps up); the targets keep the setting's size, the world drawn into a part of them
void post_set_dynamic_size(int w, int h);
int post_max_w(void);
int post_max_h(void);
// Redirects drawing to the scene target (or the screen when disabled).
void post_begin(void);
// Composes the scene onto the screen; restores the state the world drawing expects.
void post_end(void);
// Width/height currently being rendered to between post_begin and post_end.
int post_render_w(void);
int post_render_h(void);

#endif
