# Carbon ExeFile
> Wrapper to run exefile processes from a command shell without spawning a new process

## Building

In order to build the library you need access to Perforce, as that is where some of our dependencies live. The build
instructions then expect a `CCP_EVE_PERFORCE_BRANCH_PATH` environment variable that points at the location of the
Perforce branch.

Once that is set up, building can be done using the usual CMake work flow.
