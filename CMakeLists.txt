cmake_minimum_required(VERSION 2.6)

project(OCSSW)

add_executable(pdsinfo
  pdsinfo.c
)

target_link_libraries(pdsinfo
  m
)

add_executable(pdsmerge
  pdsmerge.c
)

target_link_libraries(pdsmerge
  m
)

install (TARGETS pdsinfo pdsmerge DESTINATION bin/${OCSSW_ARCH})
