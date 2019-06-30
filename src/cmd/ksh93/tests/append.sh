########################################################################
#                                                                      #
#               This software is part of the ast package               #
#          Copyright (c) 1982-2014 AT&T Intellectual Property          #
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

{
x=abc
x+=def ;} 2> /dev/null

if [[ $x != abcdef ]]
then
    log_error 'abc+def != abcdef'
fi

integer i=3
{ i+=4;} 2> /dev/null
if (( i != 7 ))
then
    log_error '3+4!=7'
fi

iarray=( one two three )
{ iarray+= (four five six) ;} 2> /dev/null
if [[ ${iarray[@]} != 'one two three four five six' ]]
then
    log_error 'index array append fails'
fi

unset iarray
iarray=one
{ iarray+= (four five six) ;} 2> /dev/null
if [[ ${iarray[@]} != 'one four five six' ]]
then
    log_error 'index array append to scalar fails'
fi

typeset -A aarray
aarray=( [1]=1 [3]=4 [xyz]=xyz )
aarray+=( [2]=2 [3]=3 [foo]=bar )
if [[ ${aarray[3]} != 3 ]]
then
    log_error 'associative array append fails'
fi

if [[ ${#aarray[@]} != 5 ]]
then
    log_error 'number of elements of associative array append fails'
fi

point=(x=1 y=2)
point+=( y=3 z=4)
if [[ ${point.y} != 3 ]]
then
    log_error 'compound append fails'
fi

if [[ ${point.x} != 1 ]]
then
    log_error 'compound append to compound variable unsets existing variables'
fi

unset foo
foo=one
foo+=(two)
if [[ ${foo[@]} != 'one two' ]]
then
    log_error 'array append to non array variable fails'
fi

unset foo
foo[0]=(x=3)
foo+=(x=4)
[[ ${foo[1].x} == 4 ]] || log_error 'compound append to index array not working'
[[ ${foo[0].x} == 3 ]] || log_error 'compound append to index array unsets existing variables'

unset foo
foo=a
foo+=''
[[ $foo == 'a' ]] || log_error 'appending an empty string not working'

unset x z arr
typeset -a x=(a b)
x+=(c d)
exp='typeset -a x=(a b c d)'
[[ $(typeset -p x) == "$exp" ]] || log_error 'append (c d) to index array not working'

typeset -a arr=(a=b b=c)
arr+=(c=d d=e)
exp='typeset -a arr=(a\=b b\=c c\=d d\=e)'
[[ $(typeset -p arr) == "$exp" ]] || log_error 'append (c=d d=e) to index array not working'

exp='typeset -a z=(a\=b b\=c d\=3 e f\=l)'
typeset -a z=(a=b b=c)
{ z+=(d=3 e f=l); } 2> /dev/null
[[ $(typeset -p z) == "$exp" ]] || log_error 'append (d=3 e f=l) to index array not working'

unset arr2
exp='typeset -a arr2=(b\=c :)'
typeset -a arr2
arr2+=(b=c :)
[[ $(typeset -p arr2) == "$exp" ]] || log_error 'append (b=c :) to index array not working'

unset arr2
exp='typeset -a arr2=(b\=c xxxxx)'
typeset -a arr2
{
    arr2+=(b=c xxxxx)
} 2> /dev/null
[[ $(typeset -p arr2) == "$exp" ]] || log_error 'append (b=c xxxxx) to index array not working'

typeset -T Foo_t=(integer x y)
Foo_t -a foo
foo+=(x=1 y=2)
foo+=(x=3 y=4)
[[ ${!foo[@]} == '0 1' ]] || log_error  'append to empty array of types not working'

foo=()
foo+=(x=1 y=2)
foo+=(x=3 y=4)
[[ ${!foo[@]} == '0 1' ]] || log_error  'append to unset array of types not working'
