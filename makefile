# Uncomment the following line to turn on debug messages
DEBUG = -DDEBUG

# Select the configuration file to use as default for 'run' 
DEFAULT_CONFIG = p2p.config

# Define compiler and flags
CC = gcc
CFLAGS = -g

# Define source and header directories
SRCDIR = src
INCDIR = include

# Define output directory
OUTDIR = output

# Define output executable
EXECUTABLE = net367

# Define source files
SRCS = $(wildcard $(SRCDIR)/*.c)

# Define object files
OBJS = $(patsubst $(SRCDIR)/%.c, $(OUTDIR)/%.o, $(SRCS))

# Define include directories
INCLUDES = -I$(INCDIR)

# Define libraries
LIBS =

# Default rule to build executable
$(EXECUTABLE): $(OBJS)
	$(CC) $(CFLAGS) $(DEBUG) $(OBJS) $(LIBS) -o $@

# Rule to build object files
$(OUTDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(DEBUG) $(INCLUDES) -c $< -o $@

# Define starting files for TestDir0
TD0_FILES = testmsg0 testmsg00
# Define starting files for TestDir1
TD1_FILES = bigTest haha.txt testmsg1 testmsg1B 

# Rule to clean object files
clean:
	rm -f $(OUTDIR)/*.o
	rm -f ./$(EXECUTABLE)
	$(foreach file, $(wildcard TestDir0/*), \
		$(if $(filter $(notdir $(file)), $(TD0_FILES)), , rm -f $(file)))
	$(foreach file, $(wildcard TestDir1/*), \
		$(if $(filter $(notdir $(file)), $(TD1_FILES)), , rm -f $(file)))

# Rule to regenerate object files and executable
regen: clean $(EXECUTABLE) run

# Rule to run the executable with the default configuration
run: $(EXECUTABLE)
	./$(EXECUTABLE) $(DEFAULT_CONFIG)