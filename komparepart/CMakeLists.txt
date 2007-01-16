
include_directories( ${CMAKE_SOURCE_DIR}/kompare/libdialogpages ${CMAKE_SOURCE_DIR}/kompare/libdiff2 ${CMAKE_SOURCE_DIR}/kompare/interfaces  )


########### next target ###############

set(komparepart_PART_SRCS 
   kompare_part.cpp 
   kompareconnectwidget.cpp 
   komparesplitter.cpp 
   komparelistview.cpp 
   kompareprefdlg.cpp 
   komparesaveoptionswidget.cpp )

kde4_automoc(${komparepart_PART_SRCS})

kde4_add_ui_files(komparepart_PART_SRCS komparesaveoptionsbase.ui )

kde4_add_plugin(komparepart WITH_PREFIX ${komparepart_PART_SRCS})



target_link_libraries(komparepart  ${KDE4_KPARTS_LIBS} dialogpages diff2 kompareinterface )

install(TARGETS komparepart  DESTINATION ${PLUGIN_INSTALL_DIR} )


########### install files ###############

install( FILES komparepart.desktop  DESTINATION  ${SERVICES_INSTALL_DIR} )
install( FILES komparepartui.rc  DESTINATION  ${DATA_INSTALL_DIR}/kompare )




#original Makefile.am contents follow:

##########################################################################
## KPART SECTION
##########################################################################
#
#INCLUDES = \
#	-I$(top_srcdir)/kompare/libdialogpages \
#	-I$(top_srcdir)/kompare/libdiff2 \
#	-I$(top_srcdir)/kompare/interfaces \
#	$(all_includes)
#
#noinst_HEADERS = \
#	kompare_part.h \
#	komparesplitter.h \
#	kompareprefdlg.h \
#	komparelistview.h \
#	kompareconnectwidget.h \
#	komparesaveoptionsbase.h \
#	komparesaveoptionswidget.h \
#	kompare_qsplitter.h
#
## let automoc handle all of the meta source files (moc)
#METASOURCES = AUTO
#
#kde_module_LTLIBRARIES = libkomparepart.la
#
## the Part's source, library search path, and link libraries
#libkomparepart_la_SOURCES = \
#	kompare_part.cpp \
#	kompareconnectwidget.cpp \
#	komparesplitter.cpp \
#	komparelistview.cpp \
#	kompareprefdlg.cpp \
#	komparesaveoptionsbase.ui \
#	komparesaveoptionswidget.cpp
#
#libkomparepart_la_LDFLAGS = $(KDE_PLUGIN) $(all_libraries)
#libkomparepart_la_LIBADD  = $(LIB_KPARTS) $(LIB_KFILE) \
#	../libdialogpages/libdialogpages.la \
#	../libdiff2/libdiff2.la \
#	../interfaces/libkompareinterface.la
#
## this is where the desktop file will go
#partdesktopdir   = $(kde_servicesdir)
#partdesktop_DATA = komparepart.desktop
#
## this is where the part's XML-GUI resource file goes
#partrcdir   = $(kde_datadir)/kompare
#partrc_DATA = komparepartui.rc
#