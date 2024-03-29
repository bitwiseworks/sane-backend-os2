/* sane - Scanner Access Now Easy.

   Copyright (C) 2019 Povilas Kanapickas <povilas@radix.lt>

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.
*/

#ifndef SANE_TESTSUITE_BACKEND_GENESYS_MINIGTEST_H
#define SANE_TESTSUITE_BACKEND_GENESYS_MINIGTEST_H

#include <cstdlib>
#include <iostream>

extern size_t s_num_successes;
extern size_t s_num_failures;

inline void print_location(std::ostream& out, const char* function, const char* path,
                           unsigned line)
{
    out << path << ":" << line << " in " << function;
}

template<class T, class U>
void check_equal(const T& t, const U& u, const char* function, const char* path, unsigned line)
{
    if (t != u) {
        s_num_failures++;
        std::cerr << "FAILURE at ";
        print_location(std::cerr, function, path, line);
        std::cerr << " :\n" << t << " != " << u << "\n\n";
    } else {
        s_num_successes++;
        std::cerr << "SUCCESS at ";
        print_location(std::cerr, function, path, line);
        std::cerr << "\n";
    }
}

inline void check_true(bool x, const char* function, const char* path, unsigned line)
{
    if (x) {
        s_num_successes++;
        std::cerr << "SUCCESS at ";
    } else {
        s_num_failures++;
        std::cerr << "FAILURE at ";
    }
    print_location(std::cerr, function, path, line);
    std::cerr << "\n";
}


#define ASSERT_EQ(x, y)  do { check_equal((x), (y), __func__, __FILE__, __LINE__); } \
                         while (false)
#define ASSERT_TRUE(x)  do { check_true(bool(x), __func__, __FILE__, __LINE__); } \
                        while (false)
#define ASSERT_FALSE(x)  do { !check_true(bool(x), __func__, __FILE__, __LINE__); } \
                         while (false)

int finish_tests();

#endif
