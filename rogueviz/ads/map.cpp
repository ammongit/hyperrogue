namespace hr {

namespace ads_game {

enum eObjType { oRock, oMissile, oParticle, oResource };

struct ads_object {
  eObjType type;
  eResourceType resource;
  cell *owner;
  ads_matrix at;
  color_t col;
  vector<ld>* shape;
  
  ld life_start, life_end;
  cross_result pt_main;
  vector<cross_result> pts;
  
  ads_object(eObjType t, cell *_owner, const ads_matrix& T, color_t _col) : type(t), owner(_owner), at(T), col(_col) { 
    life_start = -HUGE_VAL;
    life_end = HUGE_VAL;
    }
  };

enum eWalltype { wtNone, wtDestructible, wtSolid, wtGate };

struct cellinfo {
  int mpd_terrain; /* 0 = fully generated terrain */
  int rock_dist; /* rocks generated in this radius */
  vector<std::unique_ptr<ads_object>> rocks;
  eWalltype type;
  cellinfo() {
    mpd_terrain = 4;
    rock_dist = -1;
    type = wtNone;
    }
  };

std::unordered_map<cell*, cellinfo> ci_at;

using worldline_visitor = std::function<bool(cell*, ld)>;

void compute_life(cell *c, transmatrix S1, const worldline_visitor& wv) {
  ld t = 0;

  int iter = 0;
  cell *cur_c = c;
  auto cur_w = hybrid::get_where(c);
  while(t < 2 * M_PI) {
    iter++;
    auto last_w = cur_w;
    auto next_w = cur_w;
    transmatrix next_S1;
    ld next_t;
    ld last_time = t;
    cell *next_c = nullptr;
    binsearch(t, t+M_PI/2, [&] (ld t1) { 
      S1 = S1 * chg_shift(t1 - last_time);
      last_time = t1;
      virtualRebase(cur_c, S1);
      cur_w = hybrid::get_where(cur_c);
      if(cur_w.first != last_w.first) {
        next_c = cur_c;
        next_w = cur_w;
        next_S1 = S1;
        next_t = t1;
        return true;
        }
      return false;
      }, 20);
    if(!next_c) return;
    S1 = next_S1;
    cur_w = next_w;
    t = next_t;
    cur_c = next_c;
    if(iter > 1000) {
      println(hlog, "compute_life c=", cur_c, " w=", cur_w, "t=", t, " S1=", S1);
      fixmatrix_ads(S1);
      }
    if(iter > 1100) break;
    if(wv(cur_w.first, t)) break;
    }
  }

map<int, int> genstats;

int gen_budget;

void gen_terrain(cell *c, cellinfo& ci, int level = 0) {
  if(level >= ci.mpd_terrain) return;
  if(ci.mpd_terrain > level + 1) gen_terrain(c, ci, level+1);
  forCellCM(c1, c) gen_terrain(c1, ci_at[c1], level+1);
  genstats[level]++;
  
  if(level == 2) {
    int r = hrand(100);
    if(r < 3) {
      forCellCM(c1, c) if(hrand(100) < 50)
        forCellCM(c2, c1)  if(hrand(100) < 50)
          if(ci_at[c2].type == wtNone) ci_at[c2].type = wtDestructible;
      }
    else if(r < 6) {
      forCellCM(c1, c) if(hrand(100) < 50)
        forCellCM(c2, c1)  if(hrand(100) < 50)
          if(ci_at[c2].type < wtSolid)
            ci_at[c2].type = wtSolid;
      }
    else if(r < 8)
      ci_at[c].type = wtGate;
    }
  ci.mpd_terrain = level;
  }

void gen_rocks(cell *c, cellinfo& ci, int radius) {
  if(radius <= ci.rock_dist) return;
  if(ci.rock_dist < radius - 1) gen_rocks(c, ci, radius-1);
  forCellCM(c1, c) gen_rocks(c1, ci_at[c1], radius-1);
  if(geometry != gNormal) { println(hlog, "wrong geometry detected in gen_rocks 1!");  exit(1); }

  if(radius == 0) {
    hybrid::in_actual([&] {
      int q = rpoisson(.25);
      
      auto add_rock = [&] (ads_matrix T) {
        eResourceType rt = eResourceType(rand() % 6);
        auto r = std::make_unique<ads_object> (oRock, c, T, rock_color[rt]);
        r->resource = rt;
        r->shape = &(rand() % 2 ? shape_rock2 : shape_rock);
        if(geometry != gRotSpace) { println(hlog, "wrong geometry detected in gen_rocks 2!");  exit(1); }
        compute_life(hybrid::get_at(c, 0), unshift(r->at), [&] (cell *c, ld t) {
          auto& ci = ci_at[c];
          hybrid::in_underlying_geometry([&] { gen_terrain(c, ci); });
          ci.type = wtNone;
          return false;
          });
        ci.rocks.emplace_back(std::move(r));
        };
      
      for(int i=0; i<q; i++) {
        int kind = hrand(100);
        if(kind < 50) 
          add_rock(ads_matrix(rots::uxpush(randd() * .6 - .3) * rots::uypush(randd() * .6 - .3)));
        else
          add_rock(ads_matrix(rots::uypush(randd() * .6 - .3) * lorentz(0, 3, 0.5 + randd() * 1)));
        }        
      });
    }
  ci.rock_dist = radius;
  }

void gen_particles(int qty, cell *c, shiftmatrix from, color_t col, ld t, ld spread = 1) {
  auto& ro = ci_at[c].rocks;
  for(int i=0; i<qty; i++) {
    auto r = std::make_unique<ads_object>(oParticle, c, from * spin(randd() * TAU * spread) * lorentz(0, 2, 1 + randd()), col );
    r->shape = &shape_particle;
    r->life_end = randd() * t;
    r->life_start = 0;
    ro.emplace_back(std::move(r));
    }
  }

void gen_resource(cell *c, shiftmatrix from, eResourceType rsrc) {
  if(!rsrc) return;
  auto r = std::make_unique<ads_object>(oResource, c, from, rsrc_color[rsrc]);
  r->shape = rsrc_shape[rsrc];
  r->life_end = HUGE_VAL;
  r->life_start = 0;
  r->resource = rsrc;
  ci_at[c].rocks.emplace_back(std::move(r));
  }

bool pointcrash(hyperpoint h, const vector<cross_result>& vf) {
  int winding = 0;
  vector<hyperpoint> kleins;
  for(auto& p: vf) kleins.push_back(kleinize(p.h) - h);
  auto take = [&] (hyperpoint& a, hyperpoint& b) {
    if(asign(a[1], b[1]) && xcross(b[0], b[1], a[0], a[1]) < 1e-6)
      winding++;
    };
  for(int i=1; i<isize(kleins); i++) take(kleins[i-1], kleins[i]);
  take(kleins.back(), kleins[0]);
  return winding & 1;
  }

void crash_ship() {
  if(ship_pt < invincibility_pt) return;
  invincibility_pt = ship_pt + how_much_invincibility;
  pdata.hitpoints--;
  if(pdata.hitpoints <= 0) game_over = true;
  hybrid::in_actual([&] {
    cell *c = hybrid::get_where(vctr).first;
    gen_particles(16, c, ads_inverse(current * vctrV) * spin(ang*degree), rsrc_color[rtHull], 0.5);
    });
  }

void handle_crashes() {
  vector<ads_object*> missiles;
  vector<ads_object*> rocks;
  vector<ads_object*> resources;
  for(auto m: displayed) {
    if(m->type == oMissile)
      missiles.push_back(m);
    if(m->type == oRock)
      rocks.push_back(m);
    if(m->type == oResource)
      resources.push_back(m);
    }
  hybrid::in_underlying_geometry([&] {
    for(auto m: missiles) {
      hyperpoint h = kleinize(m->pt_main.h);
      for(auto r: rocks) {
        if(pointcrash(h, r->pts)) {
          m->life_end = m->pt_main.shift;
          r->life_end = r->pt_main.shift;
          hybrid::in_actual([&] {
            gen_particles(8, m->owner, m->at * ads_matrix(Id, m->life_end), missile_color, 0.1);
            gen_particles(8, r->owner, r->at * ads_matrix(Id, r->life_end), r->col, 0.5);
            gen_resource(r->owner, r->at * ads_matrix(Id, r->life_end), r->resource);
            });
          }
        }
      }
    if(!game_over) for(int i=0; i<isize(shape_ship); i+=2) {
      hyperpoint h = spin(ang*degree) * hpxyz(shape_ship[i], shape_ship[i+1], 1);
      for(auto r: rocks) {
        if(pointcrash(h, r->pts)) crash_ship();
        }
      for(auto r: resources) {
        if(pointcrash(h, r->pts)) {
          r->life_end = r->pt_main.shift;
          gain_resource(r->resource);
          }
        }
      }
    });
  }

}}
