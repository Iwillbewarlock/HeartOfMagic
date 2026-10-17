#pragma once

// =============================================================================
// SKSE FILE - the DLL's readers on real files: .skse co-saves (--cosave) and NIF headers (--nif-values)
// =============================================================================
// The reader itself is the DLL's (plugins/BlackFlame/src/CoSaveFile.h: the kPreLoadGame preflight and the total-loss
// union read a save's co-save from disk with it; NifValueNodes.h: the AddOnNode indices of a closure NIF). These modes run
// it on files: the spike's two real 1.6.1170 co-saves walk to their end with no Black Flame block; the sample request's
// 364 closure NIFs all parse and name every one of the 97 indices the builder's run made grey ADDN records for.

namespace BlackFlame::SkseFile
{
    /// --cosave <file>...: each file's walk and its BFSL state, one block a line; 0 when every file walks to its end
    int WalkFiles(int a_argc, char** a_argv);

    /// --nif-values <file>...: "<file>: <values>" per NIF; 0 when every file's header parsed
    int NifValues(int a_argc, char** a_argv);
}
