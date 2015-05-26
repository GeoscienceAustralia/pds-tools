# ===========================================================================
#	Begin Program Specific Block
# ===========================================================================

MAKEFILE = pdsmerge.mk

# Progam to make
EXE	= pdsmerge 

# Object modules for EXE
OBJ    	= pdsmerge.o 

# Library locations
LIBS 	= 

# LIBS needed to compile EXE
LLIBS 	= -lm


# Include file locations
INCLUDE = 

include $(MAKEFILE_APP_TEMPLATE)
