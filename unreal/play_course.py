# Play a round of OpenTeeGolf in the Unreal Editor (a first step: see README.md, "Play a round").
#
# OpenTeeGolf's server does the golf: it takes the shots from the launch monitor, works out each
# one's flight, bounce and roll with the game's own physics on the course, keeps the score
# (lib/unrealplay.js). This script, running inside the editor, shows it: the ball flying along each
# shot's path with the camera following, then waiting behind the ball, aimed at the pin, for the
# next shot. Messages (hole, distance, where the ball finished, score) show at the top left.
#
# Tools > Execute Python Script > play_course.py starts a round; running it again stops it.
# The course must be built in this level (build_course.py) from the same export.

import builtins
import json
import math
import threading
import time
import urllib.request
import unreal

SERVER = "http://127.0.0.1:8080"      # OpenTeeGolf (its window / the address it prints when it starts)
COURSE = "Majestic_Springs_Golf_Club"  # the course's id: its folder in courses/ and exports/
START_HOLE = 1
POLL_S = 0.25                          # how often to ask the server what's happening
BALL_CM = 4.27
# The camera: behind the ball at address (metres back, metres up, degrees down), following it in
# flight (metres back and up), and how long the ball rests before the camera goes back behind it
ADDRESS = (3.5, 1.3, 10.0)
FOLLOW = (14.0, 4.5)
REST_S = 2.0
TRACER_S = 8.0                         # how long the ball's tracer line stays (0: none)
DEBUG = False

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
viewport = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def log(msg):
    unreal.log("[OpenTeeGolf play] " + msg)


def http(path, body=None):
    req = urllib.request.Request(SERVER + path, data=None if body is None else json.dumps(body).encode("utf-8"),
                                 headers={"Content-Type": "application/json"}, method="GET" if body is None else "POST")
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read().decode("utf-8"))


def look_at(frm, to):
    return unreal.MathLibrary.find_look_at_rotation(frm, to)


def say(text, seconds=6.0, key=0):
    """A line at the top left of the viewport."""
    try:
        unreal.SystemLibrary.print_string(viewport.get_editor_world(), text, True, False, unreal.LinearColor(1, 1, 0.85, 1), seconds)
    except Exception:
        pass
    log(text)


class Round:
    def __init__(self):
        self.state = None
        self.latest = None
        self.lock = threading.Lock()
        self.running = True
        self.anim = None          # the shot being shown: {points, t}
        self.shown_shot = 0
        self.shown_seq = -1
        self.rest_until = 0
        self.next_hole_at = 0
        st = http("/api/unreal/start", {"course": COURSE, "hole": START_HOLE})
        if "error" in st:
            raise RuntimeError(st["error"])
        land = None
        for a in actors.get_all_level_actors():
            if isinstance(a, unreal.Landscape):
                land = a
                break
        if not land:
            raise RuntimeError("No landscape in this level: build the course first (build_course.py)")
        # (the landscape's corner; heights as build_course.py places them: the export's Z = 0 at
        # landscape.locationZ below the actor)
        loc = land.get_actor_location()
        self.offset = unreal.Vector(loc.x, loc.y, loc.z - st.get("landscapeZ", 0))
        self.ball = self.make_ball()
        # (the tee markers build_course.py puts down would hide the ball on the tee)
        self.hidden = [a for a in actors.get_all_level_actors() if a.get_actor_label().startswith("OTG_Hole") and a.get_actor_label().endswith(" tee")]
        for a in self.hidden:
            a.set_is_temporarily_hidden_in_editor(True)
        self.state = st
        self.shown_shot = (st.get("lastShot") or {}).get("seq", 0)
        self.place(st)
        self.thread = threading.Thread(target=self.poll, daemon=True)
        self.thread.start()
        self.tick_handle = unreal.register_slate_post_tick_callback(self.tick)
        say("%s: round started. Hit when ready - run play_course.py again to stop." % st["course"], 10)
        say(st["message"], 10)

    def make_ball(self):
        for a in actors.get_all_level_actors():
            if a.get_actor_label() == "OTG_PlayBall":
                actors.destroy_actor(a)
        sphere = unreal.load_asset("/Engine/BasicShapes/Sphere")
        b = actors.spawn_actor_from_object(sphere, unreal.Vector(0, 0, 0))
        b.set_actor_label("OTG_PlayBall")
        b.set_actor_scale3d(unreal.Vector(BALL_CM / 100.0, BALL_CM / 100.0, BALL_CM / 100.0))
        try:
            b.static_mesh_component.set_material(0, ball_material())
            b.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        except Exception:
            pass
        return b

    def at(self, p):
        return unreal.Vector(p[0] + self.offset.x, p[1] + self.offset.y, p[2] + self.offset.z)

    # ---- the server, from a thread of its own (the editor's own calls stay on its thread)
    def poll(self):
        while self.running:
            try:
                st = http("/api/unreal/state")
                with self.lock:
                    self.latest = st
            except Exception as e:
                with self.lock:
                    self.latest = {"error": str(e)}
            time.sleep(POLL_S)

    # ---- every frame
    def tick(self, dt):
        try:
            self.step(min(dt, 0.1))
        except Exception as e:
            unreal.log_warning("[OpenTeeGolf play] " + repr(e))

    def step(self, dt):
        with self.lock:
            st, self.latest = self.latest, None
        now = time.time()
        if st and "error" not in st:
            shot = st.get("lastShot")
            if shot and shot["seq"] > self.shown_shot:
                self.shown_shot = shot["seq"]
                self.anim = {"pts": shot["points"], "t": 0.0, "from": self.at(shot["points"][0][1:4])}
                say("%.0f mph, %.1f deg up, %.0f rpm" % (shot["shot"]["speedMph"], shot["shot"]["vla"], math.hypot(shot["shot"]["backSpin"], shot["shot"]["sideSpin"])), 4)
            if st["seq"] != self.shown_seq:
                self.state, self.shown_seq = st, st["seq"]
                if not self.anim:
                    self.place(st)
        if self.anim:
            self.play(dt)
        elif self.rest_until and now >= self.rest_until:
            self.rest_until = 0
            self.place(self.state)
        if self.next_hole_at and now >= self.next_hole_at:
            self.next_hole_at = 0
            try:
                self.state = http("/api/unreal/next", {})
                self.shown_seq = self.state["seq"]
                self.place(self.state)
            except Exception as e:
                unreal.log_warning("[OpenTeeGolf play] next hole: " + repr(e))

    def play(self, dt):
        a = self.anim
        a["t"] += dt
        pts = a["pts"]
        t = a["t"]
        while len(pts) > 1 and pts[1][0] <= t:
            pts.pop(0)
        if len(pts) == 1:
            p = self.at(pts[0][1:4])
            self.ball.set_actor_location(p, False, False)
            self.anim = None
            st = self.state
            say(st["message"], 8)
            if st["phase"] == "holed":
                self.next_hole_at = time.time() + 5
            elif st["phase"] == "ready":
                self.rest_until = time.time() + REST_S
            return
        q0, q1 = pts[0], pts[1]
        f = (t - q0[0]) / max(1e-6, q1[0] - q0[0])
        p = unreal.Vector(q0[1] + (q1[1] - q0[1]) * f, q0[2] + (q1[2] - q0[2]) * f, q0[3] + (q1[3] - q0[3]) * f)
        p = unreal.Vector(p.x + self.offset.x, p.y + self.offset.y, p.z + self.offset.z)
        self.ball.set_actor_location(p, False, False)
        # the tracer: the ball's path so far, a line that fades after a while
        last = a.get("last")
        if last is not None and TRACER_S > 0:
            unreal.SystemLibrary.draw_debug_line(viewport.get_editor_world(), last, p, unreal.LinearColor(1, 0.85, 0.3, 1), TRACER_S, 3.0)
        a["last"] = p
        # the camera: behind the ball along its way from the tee, up a little, looking at it
        frm = a["from"]
        dx, dy = p.x - frm.x, p.y - frm.y
        d = math.hypot(dx, dy)
        if d < 300:
            # (just after the strike: from behind the ball, turning to follow it)
            yaw = math.radians(self.state.get("aimYaw", 0))
            cam = unreal.Vector(frm.x - math.cos(yaw) * ADDRESS[0] * 100, frm.y - math.sin(yaw) * ADDRESS[0] * 100, frm.z + ADDRESS[1] * 100)
        else:
            ux, uy = dx / d, dy / d
            cam = unreal.Vector(p.x - ux * FOLLOW[0] * 100, p.y - uy * FOLLOW[0] * 100, max(p.z, frm.z) + FOLLOW[1] * 100)
        cur = getattr(self, "cam", None)
        k = min(1.0, dt * 3.0)
        cam = cam if cur is None else unreal.Vector(cur.x + (cam.x - cur.x) * k, cur.y + (cam.y - cur.y) * k, cur.z + (cam.z - cur.z) * k)
        self.cam = cam
        if DEBUG and int(t * 4) != getattr(self, "_dbg", -1):
            self._dbg = int(t * 4)
            log("t %.2f ball %s cam %s rot %s" % (t, p, cam, look_at(cam, p)))
        viewport.set_level_viewport_camera_info(cam, look_at(cam, p))

    def place(self, st):
        """The ball where it lies, and the camera behind it looking along the aim."""
        if not st:
            return
        b = self.at(st["ball"])
        self.ball.set_actor_location(b, False, False)
        yaw = math.radians(st.get("aimYaw", 0))
        back, up, down = ADDRESS
        cam = unreal.Vector(b.x - math.cos(yaw) * back * 100, b.y - math.sin(yaw) * back * 100, b.z + up * 100)
        self.cam = cam
        viewport.set_level_viewport_camera_info(cam, unreal.Rotator(roll=0, pitch=-down, yaw=st.get("aimYaw", 0)))
        if st["phase"] == "ready":
            h = st["hole"]
            say("Hole %d (par %d) · stroke %d · %d yd to the pin · %s" % (h["number"], h["par"], st["strokes"] + 1, st["toPinYd"], st["lie"]), 30)

    def stop(self):
        self.running = False
        for a in getattr(self, "hidden", []):
            try:
                a.set_is_temporarily_hidden_in_editor(False)
            except Exception:
                pass
        try:
            unreal.unregister_slate_post_tick_callback(self.tick_handle)
        except Exception:
            pass
        try:
            http("/api/unreal/stop", {})
        except Exception:
            pass
        say("Round stopped", 4)


def ball_material():
    path = "/Game/OpenTeeGolf/Play/M_GolfBall"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_GolfBall", "/Game/OpenTeeGolf/Play", unreal.Material, unreal.MaterialFactoryNew())
    c = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
    c.set_editor_property("constant", unreal.LinearColor(0.85, 0.85, 0.85, 1))
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    r = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 150)
    r.set_editor_property("r", 0.35)
    mel.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


# ---- start, or stop the round already going
old = getattr(builtins, "_otg_round", None)
if old:
    old.stop()
    builtins._otg_round = None
else:
    try:
        builtins._otg_round = Round()
    except Exception as e:
        unreal.log_error("[OpenTeeGolf play] Couldn't start: %s (is OpenTeeGolf running at %s, and the course exported?)" % (e, SERVER))
