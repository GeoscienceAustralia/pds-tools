# ===========================================================================
#	Begin Program Specific Block
# ===========================================================================

MAKEFILE = pdsinfo.mk

# Progam to make
EXE	= pdsinfo 

# Object modules for EXE
OBJ    	= pdsinfo.o 

# Library locations
LIBS 	= 

# LIBS needed to compile EXE
LLIBS 	= -lm


# Include file locations
INCLUDE = 

include $(MAKEFILE_APP_TEMPLATE)
