# The Hillside engine. Plain C++17, no JUCE, no plugin code: the envelope (Envelope, Curve), the gate from notes
# (Keys), the CC stream (CcStream) and the whole per-sample engine (Engine). The next plugin (cnoc) reuses it.

add_library(hillside_engine STATIC
    src/Envelope.cpp
    src/Keys.cpp
    src/CcStream.cpp
    src/Pulses.cpp
    src/Engine.cpp
)

target_include_directories(hillside_engine PUBLIC include)
target_compile_features(hillside_engine PUBLIC cxx_std_17)

if(HILLSIDE_BUILD_TESTS)
    add_executable(hillside_engine_test tests/engine_test.cpp)
    target_link_libraries(hillside_engine_test PRIVATE hillside_engine)
    add_test(NAME engine COMMAND hillside_engine_test)

    # Writes the engine's real MIDI stream to stdout; the mapper test (Node.js) feeds it through the Scripter mapper.
    add_executable(hillside_ccdump tests/ccdump.cpp)
    target_link_libraries(hillside_ccdump PRIVATE hillside_engine)

    find_program(NODE_EXE node)
    if(NODE_EXE)
        add_test(NAME mapper
                 COMMAND ${CMAKE_COMMAND} -E env
                         HILLSIDE_CCDUMP=$<TARGET_FILE:hillside_ccdump>
                         ${NODE_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/mapper_test.js
                         ${CMAKE_SOURCE_DIR}/tools/mapper/Hillside-Mapper.js
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/Ogdoad-Mapper.js
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/StrangeDrift-Mapper.js)
    endif()
endif()
