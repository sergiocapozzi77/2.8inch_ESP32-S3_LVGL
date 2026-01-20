file(GLOB SRCS *.c *.cpp)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS .
)
