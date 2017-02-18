log=test.log
: > $log
rc=0
for f in *_test; do
    if ! test -f $f; then
        continue
    fi

    src=$f.c
    ntestfuncs=$(egrep -c '^(int\s+)?test_' $src)
    nrun=$(egrep -c '^\s*prove_run\(test_[^)]+\);' $src)
    if test $ntestfuncs -ne $nrun; then
        echo "$f has $ntestfuncs tests but only $nrun are run" 1>&2
        rc=1
    fi

    if valgrind --leak-check=full --error-exitcode=1 -q ./$f 2>> $log; then
        echo $f PASS
    else
        echo $f ERROR
        rc=1
    fi
done
test $rc -ne 0 && echo "for details see $log"
exit $rc
