find_program(PANDOC pandoc)

if (PANDOC)
add_custom_command(OUTPUT perfecttin.pdf MAIN_DEPENDENCY perfecttin.xml COMMAND pandoc -f docbook -o perfecttin.pdf -V papersize=a4 --data-dir=${PROJECT_SOURCE_DIR}/doc ${PROJECT_SOURCE_DIR}/doc/perfecttin.xml)
add_custom_target(perfecttin-doc ALL DEPENDS perfecttin.pdf)
endif ()
