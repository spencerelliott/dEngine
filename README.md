dEngine
=======

dEngine is an OpenGL ES 2.0 renderer authored by Fabien Sanglard.

Summary :
=========

dEngine is material based. Each material result in the compilation and caching of an unique uber shader. The renderer supports:
- Shadow mapping.
- Specual mapping.
- Bump-mapping. 

Physics :
=========

The engine ships with a small rigid body simulation sized for the Dreamcast:
sphere and axis aligned box colliders, gravity, impulse based contacts,
triggers, collision layers, ray/overlap/sweep queries and a character move.
It allocates nothing at runtime and runs on a fixed timestep. See
[docs/PHYSICS.md](docs/PHYSICS.md).

Details :
=========

For details about optimizations and technical choices, check the page of the author:

http://fabiensanglard.net/dEngine/index.php

http://fabiensanglard.net/dEngineSourceCodeRelease/index.php

Enjoy,

Fabien Sanglard
