if( KICAD_3DCONNEXION_LIBSPNAV )

  add_library(spnav_thread SHARED
    "libspnav_thread.cpp"
  )

  target_include_directories(spnav_thread PRIVATE
    $<TARGET_PROPERTY:pcbnew_kiface_objects,INCLUDE_DIRECTORIES>
    ${SPNAV_INCLUDE_DIR}
  )
  target_include_directories(spnav_thread PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(spnav_thread PRIVATE
    ${SPNAV_LIBRARY}
  )

  set_target_properties(spnav_thread PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_VISIBILITY_PRESET "default"
  )

  install(TARGETS spnav_thread
    LIBRARY DESTINATION ${KICAD_LIB}
  )

endif()
