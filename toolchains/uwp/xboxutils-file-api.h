#pragma once

#include <windows.h>
#include <fileapifromapp.h>

// XboxUtils uses CreateFileW in its Windows implementation.
#define CreateFileW CreateFileFromAppW
