#!/usr/bin/env bash

####----[ ERRORS ]----------------------------------------------------------####

    # Generate error codes (emulate enum)
    ECODES=(OK ERROR EUNDEF EUNDEFP ENAN EINVOPT ECMD ECMDF EINVNUM EINVNUMC
            EINVNUMS EINVNODES ETIMEO EUNDEFT)
    for i in $(seq 0 $((${#ECODES[@]} -1 ))); do
        declare -r ${ECODES[$(($i))]}=$i;
    done

    # Register error messages
    ERRORS[$EINVOPT]='invalid option $ARG'
    ERRORS[$EUNDEF]='undefined --servers or --clients option(s)'
    ERRORS[$EUNDEFP]='undefined partition'
    ERRORS[$ENAN]='$CMD not a number'
    ERRORS[$ECMD]='$CMD not found. Please install the $PACKAGE package'
    ERRORS[$ECMDF]='$CMD failed. Exit'
    ERRORS[$EINVNUM]='invalid number of arguments'
    ERRORS[$EINVNUMC]='invalid quantity of client nodes'
    ERRORS[$EINVNUMS]='invalid quantity of server nodes'
    ERRORS[$EINVNODES]='no servers nor clients were provided. Exit'
    ERRORS[$ETIMEO]='$CMD received a timeout. Exit'
    ERRORS[$EUNDEFT]='undefined servers type. Please use --servers-type. Exit'


####----[ GENERIC FUNCTIONS ]-----------------------------------------------####

    ############################################################################
    # Check if argument is a number.                                           #
    # Args:                                                                    #
    #      -$1: Argument to check.                                             #
    # Result: return 0 if a number, else 1.                                    #
    is_numeric()
    {
       # Check number of input arguments
        if [[ "$#" -ne 1 ]]; then
            print_error $EINVNUM; return $?
        fi

        # Extract argument
        local in="$1"

        # Check if a number
        local regex='^[0-9]+$'
        if [[ "$in" =~ $regex ]]; then
            return 0
        else
            return 1
        fi
    }

    ############################################################################
    # Replace tags with content of variables.                                  #
    # Args:                                                                    #
    #      -$1: Input text.                                                    #
    #      -$2: Output variable where to store the resulting text.             #
    #      -$3: Start of the tag.                                              #
    #      -$4: End of the tag.                                                #
    # Result: store the resulting text in the variable defined by $2.          #
    tags_replace()
    {
        # Check number of input arguments
        if [[ "$#" -ne 4 ]]; then
            print_error $EINVNUM; return $?
        fi

        # Extract arguments
        local in="$1"
        local out="$2"
        local stag="$3"
        local etag="$4"

        # Search list of tags to replace
        local varList=$(echo "$in" | egrep -o "$stag[0-9A-Za-z_-]*$etag" |
                        sort -u | sed -e "s/^$stag//" -e "s/$etag$//")

        local res="$in"

        # Check if there are some tags to replace
        if [[ -n "$varList" ]]; then
            # Generate sed remplacement string
            sedOpts=''
            for var in $varList; do
                eval "value=\${${var}}"
                sedOpts="${sedOpts} -e 's#$stag${var}$etag#${value}#g'"
            done

            res=$(eval "echo -e \"\$in\" | sed $sedOpts")
        fi

        # Store resulting string in the output variable
        eval "$out=\"$res\""
    }

    ############################################################################
    # Remove all tags contained in a text.                                     #
    # Args:                                                                    #
    #      -$1: Input text.                                                    #
    #      -$2: Output variable where to store the resulting text.             #
    #      -$3: Start of the tag.                                              #
    #      -$4: End of the tag.                                                #
    # Result: store the resulting text in the variable defined by $2.          #
    tags_remove()
    {
        # Check number of input arguments
        if [[ "$#" -ne 4 ]]; then
            print_error $EINVNUM; return $?
        fi

        # Extract arguments
        local in="$1"
        local out="$2"
        local stag="$3"
        local etag="$4"

        # Remove tags
        local res="$(echo "$in" | sed -e "s#$stag[A-Za-z0-9_]*$etag##g")"

        # Store resulting string in the output variable
        eval "$out=\"$res\""
    }

    ############################################################################
    # Replace tags in a text with the content of variables.                    #
    # Args:                                                                    #
    #      -$1: Input text.                                                    #
    #      -$2: Output variable where to store the resulting text or output    #
    #           the content. ($2='var_name', $2='stdout' or $2='stderr')       #
    # Result: store or output the resulting text.                              #
    tags_replace_txt()
    {
        # Check number of input arguments
        if [[ "$#" -ne 2 ]]; then
            print_error $EINVNUM; return $?
        fi

        # Extract arguments
        local in="$1"
        local out="$2"

        # Replace all tags defined by {{TAG_NAME}}
        tags_replace "$in" "$out" '{{' '}}'

        # Check if the resulting string has to be printed in stderr or stdout
        case "$out" in
            stdout)
                eval "echo -e \"\$$out\""
                ;;
            stderr)
                eval "echo -e \"\$$out\"" 1>&2
                ;;
        esac
    }

    ############################################################################
    # Enable colors and check color depth.                                     #
    # Args:                                                                    #
    #       None                                                               #
    # Result: set global variable if color support may be turned on.           #
    enable_colors()
    {
        ENABLE_COLORS='false'

        check_cmd 'tput' 'ncurses'

        # Check if tput available for colors management
        if [[ "$?" -eq 0 ]]; then
            # Check if launched in a terminal
            if [[ -t 1 ]]; then
                local color_depth=$(tput colors)

                # Check color depth
                if [[ "$color_depth" -ge 8 ]] 2>/dev/null; then
                    print_verbose 3 'color support turned on'
                    ENABLE_COLORS='true'

                    if [[ "$color_depth" -lt 256 ]] 2>/dev/null; then
                        print_verbose 2 'color depth less than 256 (using 8)'
                    fi
                else
                    print_verbose 2 'color depth less than 8, color disabled'
                fi
            else # Not run in a terminal, turn off color support
                print_verbose 2 'not run in a terminal, color disbaled'
            fi
        else # tput not vailable, turn off color support
            print_verbose 2 'colors support turned off'
        fi
    }

    ############################################################################
    # Print text with colors if colors are enables.                            #
    # Args:                                                                    #
    #      -$1: Input text.                                                    #
    #      -$*: Other arguments for printf function.                           #
    # Result: print resulting string in stdout.                                #
    print_colors()
    {
        # Check number of input arguments
        if [[ "$#" -lt 1 ]]; then
            print_error $EINVNUM; return $?
        fi

        # Extract argument
        local in="$1<normal>"

        # Shift arguments
        shift

        # Check if colors are enabled and prepare output string
        if [[ "$ENABLE_COLORS" == "true" ]]; then
            # End tags
            local normal='$(tput sgr0)'
            local black="$normal"
            local red="$normal"
            local green="$normal"
            local grey1="$normal"
            local grey2="$normal"
            local grey3="$normal"
            local yellow="$normal"
            local blue="$normal"
            local magenta="$normal"
            local cyan="$normal"
            local white="$normal"
            local orange="$normal"
            local b="$normal"
            local i='$(tput ritm)'
            local u='$(tput rmul)'
            tags_replace "$in" 'OUT' '<\/' '>'

            # Start tags
            if [[ $(tput colors) -ge 256 ]] 2>/dev/null; then
                yellow='$(tput setaf 190)'
                orange='$(tput setaf 172)'
                grey1='$(tput setaf 240)'
                grey2='$(tput setaf 239)'
                grey3='$(tput setaf 238)'
            else
                yellow='$(tput setaf 3)'
                orange='$(tput setaf 3)'
                grey1='$(tput setaf 0)'
                grey2='$(tput setaf 0)'
                grey3='$(tput setaf 0)'
            fi

            black='$(tput setaf 0)'
            red='$(tput setaf 1)'
            green='$(tput setaf 2)'
            blue='$(tput setaf 4)'
            magenta='$(tput setaf 5)'
            cyan='$(tput setaf 6)'
            white='$(tput setaf 7)'
            b='$(tput bold)'
            i='$(tput sitm)'
            u='$(tput smul)'
            tags_replace "$OUT" 'OUT' '<' '>'
        else
            tags_remove "$in" 'OUT' '</' '>'
            tags_remove "$OUT" 'OUT' '<' '>'
        fi

        # Print string to stdout
        printf "$OUT" $*
    }

    ############################################################################
    # Print error in stderr.                                                   #
    # Args:                                                                    #
    #      -$1: Error code.                                                    #
    # Result: print error and return error code.                               #
    print_error()
    {
        # Extract argument
        local error_code="$1"

        # Check if output is not muted
        if [[ -z "$SILENT" ]]; then
            # Get error description
            eval "msg=\"${ERRORS[${error_code}]}\""

            # Print the error message
            print_colors '<red><b>Error:</b> </red>' 1>&2
            print_colors "<red>$msg</red>\n"         1>&2
        fi

        # Return the corresponding error code
        return "$error_code"
    }

    ############################################################################
    # Print warning in stderr.                                                 #
    # Args:                                                                    #
    #      -$1: message to print.                                              #
    #      -$*: printf arguments.                                              #
    # Result: print warning.                                                   #
    print_warning()
    {
        # Check if output is not muted
        if [[ -z "$SILENT" ]]; then
            # Extract argument
            local msg="$1"

            # Shift arguments
            shift

            # Print the warning message
            print_colors '<orange><b>Warning:</b> </orange>' 1>&2
            print_colors "<orange>$msg</orange>\n" $*        1>&2
        fi
    }

    ############################################################################
    # Print info in stdout.                                                    #
    # Args:                                                                    #
    #      -$1: message to print.                                              #
    #      -$*: printf arguments.                                              #
    # Result: print info message.                                              #
    print_info()
    {
        # Check if output is not muted
        if [[ -z "$SILENT" ]]; then
            # Extract argument
            local msg="$1"

            # Shift arguments
            shift

            # Print the message
            print_colors '<yellow><b>Info:</b> </yellow>'
            print_colors "<yellow>$msg</yellow>\n" $*
        fi
    }

    ############################################################################
    # Print verbose info in stdout.                                            #
    # Args:                                                                    #
    #      -$1: verbosity (1, 2 or 3).                                         #
    #      -$2: message to print.                                              #
    #      -$*: printf arguments.                                              #
    # Result: print info in verbose mod.                                       #
    print_verbose()
    {
        # Check if output is not muted
        if [[ -z "$SILENT" ]]; then
            # Extract argument
            local level="$1"
            local msg="$2"

            # Shift arguments
            shift; shift

            # Check the verbosity level currently set
            if [[ "$VERBOSE_LEVEL" -ge "$level" ]]; then
                # Select color
                local color="white"
                case "$level" in
                    1)  color="grey1";;
                    2)  color="grey2";;
                    3)  color="grey3";;
                esac

                # Print the warning message
                print_colors "<$color><b>Verbose $level:</b> </$color>"
                print_colors "<$color>$msg</$color>\n" $*
            fi
        fi
    }

    ############################################################################
    # Fold and indent input message.                                           #
    # Args:                                                                    #
    #      -$1: message to format.                                             #
    # Result: print message folded on 80 col.                                  #
    format_message()
    {
        # Extract argument
        local msg="$1"

        echo "${msg}" | fold -s | sed -r 's/^([^[:space:]])/           \1/'
    }

    ############################################################################
    # Print usage.                                                             #
    # Args:                                                                    #
    #       None                                                               #
    # Result: print short usage message.                                       #
    usage()
    {
        print_colors '<b>Usage:</b> '
        local tmp=$(head -n${SC_HSIZE:-99} "${0}" | grep -e "^#+" |
                   sed -e "s/^#+[ ]*//g" -e "s/#$//g")

        tags_replace_txt "$tmp" 'stdout'
    }

    ############################################################################
    # Print information related to development.                                #
    # Args:                                                                    #
    #       None                                                               #
    # Result: print version and contact information.                           #
    info()
    {
        local tmp=$(head -n${SC_HSIZE:-99} "${0}" | grep -e "^#-" |
                        sed -e "s/^#-//g" -e "s/#$//g" -e "s/\[at\]/@/g")

        format_message "$(tags_replace_txt "$tmp" 'stdout')"
    }

    ############################################################################
    # Print full detailled usage.                                              #
    # Args:                                                                    #
    #       None                                                               #
    # Result: print help.                                                      #
    usage_full()
    {
        local tmp=$(head -n${SC_HSIZE:-99} "${0}" | grep -e "^#[%+]" |
                       sed -e "s/^#[%+-]//g" -e "s/#$//g")

        format_message "$(tags_replace_txt "$tmp" 'stdout')"

        info
    }

    ############################################################################
    # Check if the tool is installed and the command is working on the system. #
    # Args:                                                                    #
    #       -$1: command to check.                                             #
    #       -$2: package name.                                                 #
    # Result: display an error and return error code ECMD if not installed.    #
    check_cmd()
    {
        # Check number of input arguments
        if [[ "$#" -ne 2 ]]; then
            print_error $EINVNUM; return $?
        fi

        # Extract parameters
        local cmd="$1"
        local package="$2"

        # Check if command works
        command -v $cmd >/dev/null 2>&1 ||
        {
            # Set variables for error message
            CMD=$cmd
            PACKAGE=$package

            # Print error message and return error code
            print_error $ECMD; return $?
        }

        print_verbose 3 "command %s available" "$cmd"

        return $OK
    }

    ############################################################################
    # Expand a triplet built with prefix, lower and upper numerical bounds     #
    # Args:                                                                    #
    #       -$1: prefix                                                        #
    #       -$2: lower integer bound, potentially with leading zeros           #
    #       -$3: upper integer bound, potentially with leading zeros           #
    #       -$4: suffix                                                        #
    #                                                                          #
    # Result: a space separated list of enumerated values                      #
    # or $EINVNUM if some argument are missing                                 #
    # or $EINVOPT if lower or upper bounds are not integer are missing         #
    expand_element()
    {
        # Check number of input arguments
        if [[ "$#" -lt 3 ]]; then
            print_error $EINVNUM; return $?
        fi
        local prefix=$1
        local lower=$2;
        local upper=$3;
        local suffix=$4;
        is_numeric $lower
        if [[ $? != 0   ]]; then
             print_error $EINVOPT; return $?
        fi
        is_numeric $upper
        if [[ $? != 0 ]]; then
            print_error $EINVOPT; return $?
        fi

        # seq -w manage width and takes care of leading zero
        for i in $(seq -w $lower $upper) ; do
        printf "%s%s," $prefix$i$suffix
        done
    }

    ############################################################################
    # Format the strings provided as argument to node_set                      #
    # The difficulty to solve is related to comma                              #
    # which can be used within pattern or as token separator                   #
    # e.g.:                                                                    #
    # imenode[1],ime_head_0 imeserver{1,5,7}                                   #
    #           ^_ we want to remove this comma                                #
    # is equivalent to                                                         #
    # imenode[1] ime_head_0 imeserver{1,5,7}                                   #
    # Args:                                                                    #
    #       -$1: string containing comma as token separator                    #
    # Result: string where commas are only used within patterns                #
    preprocess_node_set()
    {
        local string=$1
        local cleaned=""
        local TRIGGER=1
        for (( i=0; i<${#string}; i++ )); do
            char=${string:$i:1};
            if [[ $char =~ [\{\[] ]]; then TRIGGER=0; fi
            if [[ $char =~ [\]\}] ]]; then TRIGGER=1; fi
            # if trigger is on, the replace comma by space
            [[ $TRIGGER  != 0 ]] &&  char=${char/,/ }
            cleaned+="$char"
        done
        printf "$cleaned"
    }

############################################################################
# Expand a list potentially complex of patterns.                           #
# 3 kinds of formats are supported:                                        #
# [-] to indicate a numerical sequence that need to be expanded            #
# [,] to indicate a numerical sequence to add to a prefix                  #
# {,} to indicate a numerical sequence to add to a prefix (equ. to above)  #
#                                                                          #
# eg. node[02-6,9-10, 15-17]                                               #
# will return:                                                             #
# node02,node03,node04,node05,node06,node09,node10,node15,node16,node17    #
# Args:                                                                    #
#       -$1: string describing a list of node potentially using patterns   #
#       -$2: suffix to append to each node entry of the list               #
# Result: a comma separated list of expanded values                        #
node_set()
{
        local string=$1
        local suffix=$2
        local result=""

        string=$(preprocess_node_set "$string")
        for word in ${string[*]}; do
            # comma separated list value {,}
            if [[  "$word" =~ "{"  ]] ; then
                local prefix=`echo $word | cut -d'{' -f1`
                local list=`echo $word | cut -d'{' -f2`
                list=`echo $list | tr -d "}"`
                local sequence=(${list//,/ })
                for element in ${sequence[*]}; do
                result+="$prefix$element$suffix,"
                done
            fi
            # range value []
            if [[  "$word" =~ "["  ]] ; then
                local prefix=`echo $word | cut -d'[' -f1`
                local pattern=`echo $word | cut -d'[' -f2`
                pattern=`echo $pattern | tr -d "]"`
                # create the vector of sequence: replace ',' by ' '
                local sequence=(${pattern//,/ })

                for element in ${sequence[*]}; do
                    if [[ "$element" =~ "-" ]]; then
                        bounds=(${element//-/ })
                        result+=$(expand_element $prefix ${bounds[0]} \
                              ${bounds[1]} $suffix)
                    else
                    result+="$prefix$element$suffix,"
                    fi
                done
            fi
            # simple word
            if [[ ! "$word" =~ "{"  && ! "$word" =~ "[" ]] ; then
            result+="$word$suffix,"
            fi
        done
        # remove trailing comma if any
        printf "${result%,}"
    }
    ############################################################################
    # Check return code and exit if it is not equal to 0.                      #
    # Args:                                                                    #
    #       -$1: return code.                                                  #
    #       -$@: command with arguments which was executed.                    #
    check_return()
    {
        local exit_code=$1
        shift
        CMD="$@"

        if [[ "$exit_code" -ne 0 ]]; then
            if [[ "$exit_code" -eq 124 ]]; then
                print_error $ETIMEO
            else
                print_error $ECMDF
            fi

            if [[ "$CLEANUP" == "true" ]]; then
                my_exit "$exit_code"
            else
                exit $exit_code
            fi
        fi
    }

    ############################################################################
    # Run command and check return code.                                       #
    # Args:                                                                    #
    #       -$@: command to execute.                                           #
    run_cmd()
    {
        if [[ -n "$TIMEOUT" ]]; then
            print_info "Running (${TIMEOUT}s timeout): $(echo "$@" | xargs)"
            eval "timeout -s QUIT -k 30 --foreground $TIMEOUT $@"
        else
            print_info "Running: $(echo "$@" | xargs)"
            eval "$@"
        fi

        check_return $? "$@"
    }

