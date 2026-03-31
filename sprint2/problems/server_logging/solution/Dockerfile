FROM gcc:11.3 as build

RUN apt update && \
    apt install -y python3-pip cmake git && \
    pip3 install conan==1.*

WORKDIR /app
COPY ./conanfile.txt .
RUN mkdir build
RUN cd build && conan install .. --build=missing -s compiler.libcxx=libstdc++11

COPY ./src /app/src
COPY ./CMakeLists.txt /app/

RUN cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build .

FROM ubuntu:22.04 as run
RUN groupadd -r www && useradd -r -g www www

COPY --from=build /app/build/game_server /app/
COPY ./data /app/data
RUN chmod +x /app/game_server

USER www
ENTRYPOINT ["/app/game_server", "/app/data/config.json"]