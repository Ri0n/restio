macro(restio_setup_compiler target_name)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PUBLIC -fcoroutines-ts -stdlib=libc++)
        target_link_options(${target_name} PUBLIC -stdlib=libc++)
    else (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(${target_name} PUBLIC -fcoroutines)
    endif ()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target_name} PUBLIC -Wall -Werror -Wextra -pedantic)
        endif ()
    endif ()
    if (QT_CREATOR_COROUTINE_COMPAT)
        target_compile_definitions(${target_name} PRIVATE
            BOOST_ASIO_HAS_CO_AWAIT
            BOOST_ASIO_HAS_STD_COROUTINE
        )
    endif()
endmacro()
