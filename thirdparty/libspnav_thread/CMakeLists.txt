if( KICAD_3DCONNEXION_LIBSPNAV )

  add_library(libspnav_thread STATIC
    "libspnav_thread.cpp"
  )

  target_include_directories(libspnav_thread PRIVATE
    $<TARGET_PROPERTY:pcbnew_kiface_objects,INCLUDE_DIRECTORIES>
    ${SPNAV_INCLUDE_DIR}
  )
  target_include_directories(libspnav_thread PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(libspnav_thread
    ${SPNAV_LIBRARY}
  )

  set_target_properties(libspnav_thread PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )

endif()
