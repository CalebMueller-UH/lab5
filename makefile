# Select the configuration file to use as default for 'run'
DEFAULT_CONFIG = socketchain.config

# Define compiler and flags
CC = gcc
CFLAGS = -g -static
DEBUG = -DDEBUG
GDBFLAG = -ggdb3

# Define source and header directories
SRCDIR = src
INCDIR = include

# Define output directory
OUTDIR = output

# Create output directory if it doesn't exist
ifneq ($(wildcard $(OUTDIR)), $(OUTDIR))
$(shell mkdir -p $(OUTDIR))
endif

# Define output executables
EXECUTABLE = net367
DEBUG_EXECUTABLE = net367debug

# Define source files
SRCS = $(wildcard $(SRCDIR)/*.c)

# Define object files
OBJS = $(patsubst $(SRCDIR)/%.c, $(OUTDIR)/%.o, $(SRCS))

# Define include directories
INCLUDES = -I$(INCDIR)

# Default rule to build both executables
all: $(EXECUTABLE) $(DEBUG_EXECUTABLE)

# Rule to build the non-debug executable
$(EXECUTABLE): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIBS) -o $@

# Rule to build object files for non-debug executable
$(OUTDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Rule to build the debug executable
$(DEBUG_EXECUTABLE): $(patsubst $(OUTDIR)/%.o, $(OUTDIR)/debug_%.o, $(OBJS))
	$(CC) $(CFLAGS) $(patsubst $(OUTDIR)/%.o, $(OUTDIR)/debug_%.o, $(OBJS)) $(LIBS) -o $@

# Rule to build object files for debug executable
$(OUTDIR)/debug_%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) $(DEBUG) $(GDBFLAG) -c $< -o $@

clean:
	rm -f $(OUTDIR)/*.o
	rm -f ./$(EXECUTABLE) ./$(DEBUG_EXECUTABLE)
	$(foreach file, $(wildcard TestDir0/*), \
		$(if $(filter $(notdir $(file)), $(TD0_FILES)), , rm -f $(file)))
	$(foreach file, $(wildcard TestDir1/*), \
		$(if $(filter $(notdir $(file)), $(TD1_FILES)), , rm -f $(file)))

clear:
	clear

# Rule to regenerate object files and executables
regen: clear clean all
	
regen_d: clear clean $(DEBUG_EXECUTABLE)

# Rule to run the non-debug executable with the default configuration
run: $(EXECUTABLE)
	./$(EXECUTABLE) $(DEFAULT_CONFIG)

# Rule to run the debug executable with the default configuration
debug: $(DEBUG_EXECUTABLE)
	./$(DEBUG_EXECUTABLE) $(DEFAULT_CONFIG)

# Define starting files for TestDir0
TD0_FILES = testmsg0 testmsg00 loremipsum
# Define starting files for TestDir1
TD1_FILES = bigTest haha.txt testmsg1 testmsg1B 