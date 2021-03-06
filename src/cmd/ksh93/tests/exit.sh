########################################################################
#                                                                      #
#               This software is part of the ast package               #
#          Copyright (c) 1982-2011 AT&T Intellectual Property          #
#                      and is licensed under the                       #
#                 Eclipse Public License, Version 1.0                  #
#                    by AT&T Intellectual Property                     #
#                                                                      #
#                A copy of the License is available at                 #
#          http://www.eclipse.org/org/documents/epl-v10.html           #
#         (with md5 checksum b35adb5213ca9657e911e9befb180842)         #
#                                                                      #
#              Information and Software Systems Research               #
#                            AT&T Research                             #
#                           Florham Park NJ                            #
#                                                                      #
#                    David Korn <dgkorn@gmail.com>                     #
#                                                                      #
########################################################################

# Test for proper exit of shell

print exit 0 >.profile
${SHELL}  <<!
HOME='$PWD' exec -c -a -ksh ${SHELL} -c "exit 1"
!
status=$(echo $?)
if [[ -o noprivileged && $status != 0 ]]
then
    log_error 'exit in .profile is ignored'
elif [[ -o privileged && $status == 0 ]]
then
    log_error 'privileged .profile not ignored'
fi

if [[ $(trap 'code=$?; echo $code; trap 0; exit $code' 0; exit 123) != 123 ]]
then
    log_error 'exit not setting $?'
fi

cat > run.sh <<- "EOF"
    trap 'code=$?; echo $code; trap 0; exit $code' 0
    ( trap 0; exit 123 )
EOF
if [[ $($SHELL ./run.sh) != 123 ]]
then
    log_error 'subshell trap on exit overwrites parent trap'
fi

# ========
# If haven't already done a `cd`, or `OLDPWD` wasn't imported from the environment then we need to
# ensure it is defined for `cd ~-` to work.
cd $TEST_DIR || { err_exit "cd $TEST_DIR failed"; exit 1; }
cd /
cd ~- || log_error 'cd ~- (i.e., cd $OLDPWD) failed'
log_warning "$(pwd)" "$OLDPWD" ''
