# Hearthline Homes - Procedural House Exteriors

Copyright __PUBLISHER__. All Rights Reserved.

Hearthline Homes builds suburban house exteriors from properties instead of from hand-placed pieces.
Drop a **Hearthline House** actor in a level, pick a style, press **Randomize**, then change anything you like:
footprint, storeys, roof type and pitch, cladding, colours, windows, doors, porches, garage, dormers, chimney.
When the house is right, **Bake To Static Mesh** turns it into an ordinary static mesh asset.

The pack is exterior only. Houses are closed shells with blinds behind the glass; there are no interiors.

Engine version: Unreal Engine 5.8. Platform tested: Windows (Win64).

## Installing

1. Copy the `HearthlineHomes` folder into your project's `Plugins` folder (or install it to the engine from Fab).
2. Enable **Hearthline Homes** under Edit > Plugins and restart the editor.
3. In the Content Browser turn on **Show Plugin Content**. The demo map is `HearthlineHomes/Maps/HearthlineDemo`.

The plugin has one runtime C++ module and needs no other plugins.

## Quick start

1. Place Actors panel > search **Hearthline House** > drag it into the level. The actor's +X axis is the front of the house.
2. In Details > **Hearthline | Style** choose a Style and press **Randomize** until you like the shape.
3. Press **Randomize Colors** to keep the shape and try other finishes.
4. Edit any property by hand. The shell rebuilds as you change values.
5. Optional: turn on **Lot** for a lawn, driveway and front path. Place a **Hearthline Street** actor for the road.
6. Press **Bake To Static Mesh** before shipping (see Baking below).

## Styles

| Style | What Randomize produces |
| --- | --- |
| Ranch | One storey, long and low, hip or gable roof, attached garage wing, optional entry porch |
| Colonial | Two storeys, symmetric five-bay front, shutters, end chimney, optional portico and garage wing |
| Cape Cod | One storey under a steep roof with two or three dormers and a central chimney |
| Craftsman | Front-facing gable, full-width porch on masonry piers, accent cladding in the gable |
| Farmhouse | Two-storey front gable with a side wing and porch, board and batten, dark sashes |
| Modern | Two flat-roofed boxes with parapets, wide undivided windows, no trim |
| Cottage | Small, steep roof with a projecting front gable, stucco or stone |
| Split Level | One-storey half raised beside a two-storey half with the garage underneath |

A style only fills in properties. Nothing is locked afterwards.

## Property reference

### Shape

- **Volumes**: the boxes that make up the house. Each has an Offset from the actor origin, Width (across the front), Depth,
  Storeys, Storey Height, Floor Offset (for split levels) and its own roof. Overlap or butt volumes together to make L, T and U plans.
  Walls hidden inside another volume get no windows, and a gable roof that runs into a neighbour is extended into it so the valley closes.
  - **Roof**: Gable, Hip, Shed or Flat. **Ridge Along Width** off turns the gable to face the front. Pitch is in degrees.
    Dormers Front/Back add gabled dormers to a gable roof. Flip Shed swaps the high side of a shed roof.
  - **Window Sides**: which sides get automatic windows.
  - **Cladding / Wall Color** overrides: give one volume a different finish from the rest of the house.
- **Porches**: attach to a side of a volume between Start and End (0 to 1, left to right seen from outside). Depth, roof and
  pitch, railing, post size, masonry piers, step position and width.
- **Chimney**: volume, position (Across Width and Along Depth; values just outside 0..1 stand the stack against the outside wall),
  size, height above the roof, finish.

### Walls, foundation, roof

- **Cladding**: Lap Siding, Board And Batten, Brick, Stucco, Field Stone or Concrete, with a Wall Color tint.
- **Accent**: None, Gables (gable ends and dormers) or Upper Storeys, with its own cladding and colour.
- **Trim Color, Corner Boards, Band Boards, Gable Vents** (a tall gable gets an attic window instead of a vent).
- **Foundation**: height, finish, colour. Garage doors cut through it to reach the ground.
- **Roof**: colour, Fascia Depth, Gutters with downspouts, Parapet Height for flat roofs.

### Windows

Width, Height, Sill Height, **Window Spacing** (target distance between centres; windows are spread evenly over each free stretch of wall),
glazing bar columns and rows, Double Hung, Shutters, Casing Width (0 for a trimless modern look), Sash Color and
**Lit Window Fraction** (share of windows that glow for dusk and night scenes; in full daylight a lit window looks washed out,
so leave it at 0 for daytime levels). Which windows are lit follows the Seed.

### Front door and garage

- Door: which volume and side, Position along the wall, Style (Six Panel, Half Glass, Craftsman, Modern), colour, size, Sidelights.
  A door that is not under a porch gets a stoop and steps sized to the foundation height.
- Garage: volume, side, position, 1 to 3 doors, door size, colour, window row.

### Openings (manual)

Add entries to **Openings** to place extra windows, doors, garage doors or Blank zones on a given volume, side and storey.
Automatic windows keep clear of them. To place every window by hand, clear a volume's Window Sides and add them here.

### Lot

Lawn rectangle, driveway from the garage and path from the front door (or porch steps) to the front edge of the lot.

### Planting and fence

- **Foundation Shrubs** with **Shrub Density**: rounded and upright shrubs along the front and side walls, clear of doors,
  garage doors, porches, the driveway and the path.
- **Lot Trees** and **Tree Height** (needs Lot): broadleaf trees placed clear of the house and paving, the first one in the front yard.
- **Foliage Color** tints all plants. **Plant Seed** rearranges them without changing the house.
- **Fence** (needs Lot): Picket or Privacy style around the back yard, with returns to the sides of the house. **Fence Sides**
  chooses which lot edges are fenced, so neighbouring lots do not double up on a shared line. Fence Height and Fence Color.

Plants are generated geometry (limbs, a dark inner mass and alpha-masked leaf cards) and are baked into the static mesh with the house.
They do not sway in wind.

### Materials

Fourteen material slots (five claddings, concrete, shingles, trim, wood, glass, metal, asphalt, grass, foliage). Replace any of them with your own.
Colours are carried in vertex colour, so one material serves every house regardless of tint.

To use your own tileable textures: duplicate an instance of `M_HH_Surface` (for example `MI_HH_Brick`), assign Base Color, Normal and
ORM (R = ambient occlusion, G = roughness, B = metallic) textures, set Tiling (1 = one repeat per metre) and put the instance
in the house's Materials set. A custom material only needs to multiply its base colour by vertex colour to pick up the tints.

## Baking

A live house is a dynamic mesh: quick to edit, but it does not take part in Nanite or in Lumen's scene (it is lit and
shadowed correctly, but it does not bounce light or appear in software ray traced reflections).

**Bake To Static Mesh** (editor only) writes the shell to a static mesh asset named `SM_HH_<actor label>` in **Bake Folder**
(default `/Game/HearthlineBaked`), with Nanite on if **Bake With Nanite** is set, generated lightmap UVs and complex collision.
The actor then shows that asset. **Clear Bake** returns to live editing; the asset is left in place. Changing properties while
baked has no visible effect until you bake again.

## Runtime use

`AHearthlineHouse` is Blueprintable and every property is Blueprint read/write. Set properties, then call **Rebuild**,
**Apply Style** or **Randomize**. Generation happens on the game thread; a typical house is 5,000 to 25,000 triangles.

## Hearthline Street

A straight street along the actor's X axis: carriageway with a dashed centre line, curbs, grass verges and sidewalks.
Length, Road Width, Curb Height, Verge Width and Sidewalk Width are editable. **Tree Spacing** above 0 plants street trees in the verges.
The street has no driveway aprons: driveways stop at the lot edge.

## Content layout

```
HearthlineHomes/
  Maps/        HearthlineDemo (a street with one baked house of every style plus a second ranch)
  Demo/        SM_HH_House_* (the baked meshes used by the demo map; select a house and press Clear Bake to edit it live)
  Materials/   M_HH_Surface, M_HH_Paint, M_HH_Glass and their instances (MI_HH_*)
  Textures/    T_HH_<Surface>_D, _N, _ORM (2048 x 2048)
```

All textures are original and were generated procedurally for this product.

## Known limits

- Exterior only: no interiors, furniture or props.
- One tree type and two shrub shapes; plants are static and fairly simple up close.
- Volumes are axis-aligned boxes; there are no curved or angled walls.
- Roofs of neighbouring volumes intersect rather than being trimmed against each other. This looks right for the usual
  cross-gable and wing layouts but can leave odd overlaps in unusual ones.
- Lawns are a flat textured surface without grass blades.
