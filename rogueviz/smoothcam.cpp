#include "rogueviz.h"

// This module allows creating complex animations with smooth camera movement

// to add: insert positions? split/merge segments? edit front_distance and up_distance?

namespace hr {

using pcell = cell*;

void hwrite(hstream& hs, const pcell& c) {
  hs.write<int>(mapstream::cellids[c]);
  }

void hread(hstream& hs, pcell& c) {
  int32_t at = hs.get<int>();
  c = mapstream::cellbyid[at];
  }
  
namespace smoothcam {

string smooth_camera_help = 
  "This feature lets you create animations with complex but smooth camera movement.\n\n"
  "An animation is composed from a number of segments.\n\n"
  "In each segment, you can provide a number of positions, and times for them. "
  "For example, if you add a camera position A at time 0 and a camera position B at time 1, "
  "we will move linearly from A to B. Polynomial approximation is used inside a segment, "
  "while separate segments are animated independently.\n\n"
  "The 'interval' values are the interval between the current and next position. "
  "The total sum of 'interval' values is made equal to the 'animation period'. "
  "If you place two positions X and Y with interval 0 between them, X will be used"
  "as the actual position, while Y-X will be the first derivative. Thus, for example, "
  "placing two equal positions with interval 0 will force the camera to smoothly stop.";

struct frame {
  string title;
  cell *where;
  transmatrix sView;
  transmatrix V;
  transmatrix ori;
  ld front_distance, up_distance;
  ld interval;
  };

struct animation {
  cell *start_cell;
  transmatrix start;
  ld start_interval;
  vector<frame> frames;
  };

map<cell*, map<hyperpoint, string> > labels;
map<cell*, vector<vector<hyperpoint> > > traces;

vector<animation> anims;

transmatrix last_view, current_position, last_view_comp;
cell *last_centerover;

// during the animation, transform original coordinates to the current view coordinates
transmatrix last_computed;
ld last_time;

void analyze_view_pre() {
  current_position = current_position * last_view * inverse(View);
  }

void analyze_view_post() {
  last_view = View;
  }

void start_segment() {
  anims.emplace_back();
  auto& anim = anims.back();
  anim.start_cell = centerover;
  anim.start = Id;
  last_view = Id;
  current_position = Id;
  }

/** does not work correctly -- should adjust to the current cell */
void join_segment() {
  int n = anims.back().frames.size();
  if(n < 2) return;
  auto s1 = anims.back().frames[n-2];
  auto s2 = anims.back().frames[n-1];
  start_segment();
  auto& l = anims[anims.size()-2];
  anims.back().frames.push_back(l.frames[n-2]);
  anims.back().frames.push_back(l.frames[n-1]);
  anims.back().start_cell = l.start_cell;
  anims.back().start = l.start;
  anims.back().start_interval = 0;
  }

map<cell*, int> indices;

string gentitle() {
  return lalign(0, centerover, ":", indices[centerover]++);
  }

bool animate_on;
bool view_labels, view_trace;

void edit_interval(ld& v) {
  dialog::add_action([&v] {
    dialog::editNumber(v, -10, 10, 1, 0, "interval", "");
    });
  }

void edit_segment(int aid) {
  cmode = sm::SIDE;
  gamescreen(0);
  dialog::init(XLAT("animation segment"), 0xFFFFFFFF, 150, 0);
  dialog::addSelItem("interval", fts(anims[aid].start_interval), 'i');
  edit_interval(anims[aid].start_interval);
  dialog::addItem("delete", 'd');
  dialog::add_action([aid] {
    anims.erase(anims.begin()+aid);
    if(anims.empty()) start_segment();
    popScreen();
    });
  dialog::addItem("mirror", 'm');
  dialog::add_action([aid] {
    auto a = anims[aid];
    reverse(a.frames.begin(), a.frames.end());
    ld* last = &a.start_interval;
    for(auto& f: a.frames) { swap(*last, f.interval); last = &f.interval; }
    anims.push_back(std::move(a));
    popScreen();
    });
  dialog::addBack();
  dialog::display();
  }

void generate_trace();

void edit_step(animation& anim, int id) {
  cmode = sm::SIDE;
  gamescreen(0);
  dialog::init(XLAT("animation step"), 0xFFFFFFFF, 150, 0);
  auto& f = anim.frames[id];
  dialog::addSelItem("title", f.title, 't');
  dialog::addSelItem("interval", fts(f.interval), 'i');
  edit_interval(f.interval);
  dialog::addItem("delete", 'd');
  dialog::add_action([&anim, id] {
    anim.frames.erase(anim.frames.begin()+id);
    popScreen();
    });
  dialog::addItem("edit", 'e');
  dialog::add_action([&f] {
    f.where = centerover;
    f.sView = View;
    f.V = current_position;
    });
  dialog::addItem("recall", 'r');
  dialog::add_action([&f] {
    View = f.sView * calc_relative_matrix(centerover, f.where, inverse(View) * C0);
    NLP = ortho_inverse(f.ori);
    });
  dialog::addBack();
  dialog::display();
  }

void show() {
  cmode = sm::SIDE;
  gamescreen(0);
  dialog::init(XLAT("smooth camera"), 0xFFFFFFFF, 150, 0);
  char key = 'A';
  int aid = 0;
  
  labels.clear();
  
  for(auto& anim: anims) {
    dialog::addSelItem("segment", fts(anim.start_interval), key++);
    dialog::add_action_push([aid] { edit_segment(aid); });
    int id = 0;
    for(auto& f: anim.frames) {
      labels[f.where][inverse(f.sView) * C0] = f.title;
      dialog::addSelItem(f.title + " [" + its(celldistance(f.where, centerover)) + "]", fts(f.interval), key++);
      dialog::add_action_push([&anim, id] { edit_step(anim, id); });
      id++;
      }
    aid++;
    }

  dialog::addItem("create a new position", 'a');
  dialog::add_action([] {
    println(hlog, "current_position is ", current_position * C0);
    anims.back().frames.push_back(frame{gentitle(), centerover, View, current_position, ortho_inverse(NLP), 1, 1, 0});
    });

  dialog::addItem("create a new segment", 'b');
  dialog::add_action(start_segment);

  dialog::addItem("increase interval by 1", 's');
  dialog::add_key_action('s', [] {
    if(!anims.back().frames.empty())
      anims.back().frames.back().interval += 1;
    else
      anims.back().start_interval+=1;
    });

  /* dialog::addItem("join a new segment", 'j');
  dialog::add_action(join_segment); */

  dialog::addBoolItem_action("view the labels", view_labels, 'l');
  dialog::addBoolItem("view the trace", view_trace, 't');
  dialog::add_action([] {
    view_trace = !view_trace;
    if(view_trace) generate_trace();
    });

  dialog::addBoolItem("run the animation", animate_on, 'r');
  dialog::add_action([] {
    animate_on = !animate_on;
    last_time = HUGE_VAL;
    });
    
  dialog::addHelp(smooth_camera_help);
  dialog::addBack();
  dialog::display();
  
  keyhandler = [] (int sym, int uni) {
    handlePanning(sym, uni);
    dialog::handleNavigation(sym, uni);
    if(doexiton(sym, uni)) popScreen();
    };
  }

int last_segment;

void handle_animation(ld t) {
  
  ld total_total;
  
  vector<ld> totals;
  for(auto& anim: anims) {
    ld total = anim.start_interval;
    for(auto& f: anim.frames)
      total += f.interval;
    totals.push_back(total);
    total_total += total;
    }
    
  if(total_total == 0) return;

  t = frac(t);
  t *= total_total;
  int segment = 0;
  while(totals[segment] < t && segment < isize(totals)-1) t -= totals[segment++];
  
  auto& anim = anims[segment];

  if(t < last_time || segment != last_segment) {
    last_time = 0;
    last_segment = segment;
    View = anim.start;
    last_view_comp = View;
    centerover = anim.start_cell;
    }

  ld total = anim.start_interval;
  vector<ld> times;
  for(auto& f: anim.frames) {
    times.push_back(total);
    total += f.interval;
    }

  hyperpoint pts[3];
  
  for(int j=0; j<3; j++) {
    for(int i=0; i<MDIM; i++) {
      vector<ld> values;
      for(auto& f: anim.frames) {
        hyperpoint h;
        if(j == 0)
          h = tC0(f.V);
        if(j == 1) {
          h = tC0(parallel_transport(f.V, f.ori, zpush0(f.front_distance)));
          }
        if(j == 2) {
          h = tC0(parallel_transport(f.V, f.ori, ypush0(-f.up_distance)));
          }
        values.push_back(h[i]);
        }
      
      int n = isize(values);
      
      for(int ss=1; ss<=n-1; ss++)
        for(int a=0; a<n-ss; a++) {
          // combining [a..a+(ss-1)] and [a+1..a+ss]
          if(times[a+ss] == times[a])
            values[a] = (values[a+ss] - values[a]) * (t-times[a]);
          else
            values[a] = (values[a] * (times[a+ss] - t) + values[a+1] * (t - times[a])) / (times[a+ss] - times[a]);
          }
      
      pts[j][i] = values[0];
      }
    pts[j] = normalize(pts[j]);
    }
  
  transmatrix V = View;
  set_view(pts[0], pts[1], pts[2]);

  transmatrix T = View * inverse(last_view_comp);
  last_view_comp = View;
  
  View = T * V;
  fixmatrix(View);
  
  if(invalid_matrix(View)) {
    println(hlog, "invalid_matrix ", View);
    println(hlog, pts[0]);
    println(hlog, pts[1]);
    println(hlog, pts[2]);
    println(hlog, "t = ", t);
    exit(1);
    }
  last_time = t;
  }

void handle_animation0() {
  if(!animate_on) return;
  handle_animation(ticks / anims::period);
  anims::moved();
  }

void generate_trace() {
  last_time = HUGE_VAL;
  dynamicval<transmatrix> tN(NLP, NLP);
  dynamicval<transmatrix> tV(View, View);
  dynamicval<transmatrix> tC(current_display->which_copy, current_display->which_copy);
  dynamicval<cell*> tc(centerover, centerover);
  cell* cview = nullptr;
  vector<hyperpoint> at;
  traces.clear();
  auto send = [&] {
    if(cview && !at.empty()) traces[cview].push_back(at);
    cview = centerover;
    at.clear();
    };
  for(ld t=0; t<=1024; t ++) {
    handle_animation(t / 1024);
    if(cview != centerover) send();
    at.push_back(inverse(View) * C0);
    optimizeview();
    if(cview != centerover) {
      send();
      at.push_back(inverse(View) * C0);
      }
    }
  send();
  }

void hwrite(hstream& hs, const animation& anim) {
  hwrite(hs, anim.start_cell, anim.start, anim.start_interval, anim.frames);
  }

void hread(hstream& hs, animation& anim) {
  hread(hs, anim.start_cell, anim.start, anim.start_interval, anim.frames);
  }

void hwrite(hstream& hs, const frame& frame) {
  hwrite(hs, frame.title, frame.where, frame.sView, frame.V, frame.ori, frame.front_distance, frame.up_distance, frame.interval);
  }

void hread(hstream& hs, frame& frame) {
  hread(hs, frame.title, frame.where, frame.sView, frame.V, frame.ori, frame.front_distance, frame.up_distance, frame.interval);
  }

bool draw_labels(cell *c, const shiftmatrix& V) {
  if(view_labels) for(auto& p: labels[c])
    queuestr(V * rgpushxto0(p.first), .1, p.second, 0xFFFFFFFF, 1);
  if(view_trace) 
    for(auto& v: traces[c]) {
      for(auto p: v)
        curvepoint(p);
      queuecurve(V, 0xFFD500FF, 0, PPR::FLOOR);
      for(auto p: v)
        curvepoint(p);
      queuecurve(V, 0x80000080, 0, PPR::SUPERLINE);
      }
  return false;
  }

bool enabled;

void enable() { 
  if(enabled) return;
  enabled = true;
  rogueviz::cleanup.push_back([] { enabled = false; });
  rogueviz::rv_hook(hooks_preoptimize, 75, analyze_view_pre);
  rogueviz::rv_hook(hooks_postoptimize, 75, analyze_view_post);
  rogueviz::rv_hook(anims::hooks_anim, 100, handle_animation0);
  rogueviz::rv_hook(hooks_drawcell, 100, draw_labels);
  rogueviz::rv_hook(mapstream::hooks_savemap, 100, [] (fhstream& f) {
    f.write<int>(17);
    hwrite(f, anims);
    });
  anims.clear();
  start_segment();
  }

void enable_and_show() {
  showstartmenu = false;
  start_game();
  enable();
  pushScreen(show);
  }

auto hooks = arg::add3("-smoothcam", enable_and_show)
  + addHook(dialog::hooks_display_dialog, 100, [] () {
    if(current_screen_cfunction() == anims::show) {
      dialog::addItem(XLAT("smooth camera"), 'C'); 
      dialog::add_action(enable_and_show);
      }
    }) +
  + addHook(mapstream::hooks_loadmap, 100, [] (fhstream& f, int id) {
    if(id == 17) {
      enable();
      hread(f, anims);
      }
    });

}}
