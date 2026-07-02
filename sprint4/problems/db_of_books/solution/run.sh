#!/bin/bash
cmake --build ./build/. -j 8 && ./build/book_manager postgres://postgres:Mys3Cr3t@localhost:30432/book_manager