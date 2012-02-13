/* 
 * Copyright © 2012 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Benjamin Segovia <benjamin.segovia@intel.com>
 */

/**
 * \file assert.cpp
 *
 * \author Benjamin Segovia <benjamin.segovia@intel.com>
 */
#if GBE_COMPILE_UTESTS

#include "assert.hpp"
#include "exception.hpp"
namespace gbe
{
  void onFailedAssert(const char *file, const char *fn, int line)
  {
    char lineString[256];
    sprintf(lineString, "%i", line);
    const std::string msg = "file " + std::string(file)
                          + ", function " + std::string(fn)
                          + ", line " + std::string(lineString);
    throw Exception(msg);
  }
} /* namespace gbe */

#else

#include "sys/assert.hpp"
#include "sys/exception.hpp"
#include "sys/platform.hpp"
#include <cstdio>

namespace gbe
{
  void onFailedAssert(const char *file, const char *fn, int32_t line)
  {
    fprintf(stderr, " ASSERTION FAILED: file %s, function %s, line %i\n",
            file, fn, line);
    fflush(stdout);
    DEBUGBREAK();
    _exit(-1);
  }
} /* namespace gbe */

#endif /* GBE_COMPILE_UTESTS */

