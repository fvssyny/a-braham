#!/usr/bin/env python3

# This script manages littlefs tests, which are configured with
# .toml files stored in the tests directory.
#

import toml
import glob
import re
import os
import io
import itertools as it
import collections.abc as abc
import subprocess as sp
import base64
import sys
import copy
import shutil

TEST_DIR = 'tests_'

RULES = """
define FLATTEN
%$(subst /,.,$(target:.c=.t.c)): $(target)
    cat <(echo '#line 1 "$$<"') $$< > $$@
endef
$(foreach target,$(SRC),$(eval $(FLATTEN)))

-include tests_/*.d

%.c: %.t.c
    ./scripts/explode_asserts.py $< -o $@

%.test: %.test.o $(foreach f,$(subst /,.,$(SRC:.c=.o)),%.test.$f)
    $(CC) $(CFLAGS) $^ $(LFLAGS) -o $@
"""
GLOBALS = """
//////////////// AUTOGENERATED TEST ////////////////
#include "lfs.h"
#include "emubd/lfs_emubd.h"
#include <stdio.h>
"""
DEFINES = {
    "LFS_READ_SIZE": 16,
    "LFS_PROG_SIZE": "LFS_READ_SIZE",
    "LFS_BLOCK_SIZE": 512,
    "LFS_BLOCK_COUNT": 1024,
    "LFS_BLOCK_CYCLES": 1024,
    "LFS_CACHE_SIZE": "(64 % LFS_PROG_SIZE == 0 ? 64 : LFS_PROG_SIZE)",
    "LFS_LOOKAHEAD_SIZE": 16,
}
PROLOGUE = """
    // prologue
    __attribute__((unused)) lfs_t lfs;
    __attribute__((unused)) lfs_emubd_t bd;
    __attribute__((unused)) lfs_file_t file;
    __attribute__((unused)) lfs_dir_t dir;
    __attribute__((unused)) struct lfs_info info;
    __attribute__((unused)) uint8_t buffer[1024];
    __attribute__((unused)) char path[1024];

    __attribute__((unused)) const struct lfs_config cfg = {
        .context = &bd,                      
        .read  = &lfs_emubd_read,            
        .prog  = &lfs_emubd_prog,            
        .erase = &lfs_emubd_erase,           
        .sync  = &lfs_emubd_sync,            
                                             
        .read_size      = LFS_READ_SIZE,     
        .prog_size      = LFS_PROG_SIZE,     
        .block_size     = LFS_BLOCK_SIZE,    
        .block_count    = LFS_BLOCK_COUNT,   
        .block_cycles   = LFS_BLOCK_CYCLES,  
        .cache_size     = LFS_CACHE_SIZE,    
        .lookahead_size = LFS_LOOKAHEAD_SIZE,
    };

    lfs_emubd_create(&cfg, "blocks");
"""
EPILOGUE = """
    // epilogue
    lfs_emubd_destroy(&cfg);
"""
PASS = '\033[32m✓\033[0m'
FAIL = '\033[31m✗\033[0m'

class TestFailure(Exception):
    def __init__(self, case, stdout=None, assert_=None):
        self.case = case
        self.stdout = stdout
        self.assert_ = assert_

class TestCase:
    def __init__(self, suite, config, caseno=None, lineno=None, **_):
        self.suite = suite
        self.caseno = caseno
        self.lineno = lineno

        self.code = config['code']
        self.defines = config.get('define', {})
        self.leaky = config.get('leaky', False)

    def __str__(self):
        if hasattr(self, 'permno'):
            return '%s[%d,%d]' % (self.suite.name, self.caseno, self.permno)
        else:
            return '%s[%d]' % (self.suite.name, self.caseno)

    def permute(self, defines, permno=None, **_):
        ncase = copy.copy(self)
        ncase.case = self
        ncase.perms = [ncase]
        ncase.permno = permno
        ncase.defines = defines
        return ncase

    def build(self, f, **_):
        # prologue
        f.write('void test_case%d(' % self.caseno)
        first = True
        for k, v in sorted(self.perms[0].defines.items()):
            if k not in self.defines:
                if not first:
                    f.write(',')
                else:
                    first = False
                f.write('\n')
                f.write(8*' '+'__attribute__((unused)) intmax_t %s' % k)
        f.write(') {\n')

        for k, v in sorted(self.defines.items()):
            f.write(4*' '+'#define %s %s\n' % (k, v))

        f.write(PROLOGUE)
        f.write('\n')
        f.write(4*' '+'// test case %d\n' % self.caseno)
        f.write(4*' '+'#line %d "%s"\n' % (self.lineno, self.suite.path))

        # test case goes here
        f.write(self.code)

        # epilogue
        f.write(EPILOGUE)
        f.write('\n')

        for k, v in sorted(self.defines.items()):
            f.write(4*' '+'#undef %s\n' % k)

        f.write('}\n')

    def test(self, **args):
        # clear disk first
        shutil.rmtree('blocks')

        # build command
        cmd = ['./%s.test' % self.suite.path,
            repr(self.caseno), repr(self.permno)]

        # run in valgrind?
        if args.get('valgrind', False) and not self.leaky:
            cmd = ['valgrind',
                '--leak-check=full',
                '--error-exitcode=4',
                '-q'] + cmd

        # run test case!
        stdout = []
        if args.get('verbose', False):
            print(' '.join(cmd))
        proc = sp.Popen(cmd,
            universal_newlines=True,
            bufsize=1,
            stdout=sp.PIPE,
            stderr=sp.STDOUT)
        for line in iter(proc.stdout.readline, ''):
            stdout.append(line)
            if args.get('verbose', False):
                sys.stdout.write(line)
        proc.wait()

        if proc.returncode != 0:
            # failed, try to parse assert?
            assert_ = None
            for line in stdout:
                try:
                    m = re.match('^([^:\\n]+):([0-9]+):assert: (.*)$', line)
                    # found an assert, print info from file
                    with open(m.group(1)) as f:
                        lineno = int(m.group(2))
                        line = next(it.islice(f, lineno-1, None)).strip('\n')
                        assert_ = {
                            'path': m.group(1),
                            'lineno': lineno,
                            'line': line,
                            'message': m.group(3),
                        }
                except:
                    pass

            self.result = TestFailure(self, stdout, assert_)
            raise self.result

        else:
            self.result = PASS
            return self.result

class TestSuite:
    def __init__(self, path, TestCase=TestCase, **args):
        self.name = os.path.basename(path)
        if self.name.endswith('.toml'):
            self.name = self.name[:-len('.toml')]
        self.path = path
        self.TestCase = TestCase

        with open(path) as f:
            # load tests
            config = toml.load(f)

            # find line numbers
            f.seek(0)
            linenos = []
            for i, line in enumerate(f):
                if re.match(r'^\s*code\s*=\s*(\'\'\'|""")', line):
                    linenos.append(i + 2)

        # grab global config
        self.defines = config.get('define', {})

        # create initial test cases
        self.cases = []
        for i, (case, lineno) in enumerate(zip(config['case'], linenos)):
            self.cases.append(self.TestCase(
                self, case, caseno=i, lineno=lineno, **args))

    def __str__(self):
        return self.name

    def __lt__(self, other):
        return self.name < other.name

    def permute(self, defines={}, **args):
        for case in self.cases:
            # lets find all parameterized definitions, in one of [args.D,
            # suite.defines, case.defines, DEFINES]. Note that each of these
            # can be either a dict of defines, or a list of dicts, expressing
            # an initial set of permutations.
            pending = [{}]
            for inits in [defines, self.defines, case.defines, DEFINES]:
                if not isinstance(inits, list):
                    inits = [inits]

                npending = []
                for init, pinit in it.product(inits, pending):
                    ninit = pinit.copy()
                    for k, v in init.items():
                        if k not in ninit:
                            try:
                                ninit[k] = eval(v)
                            except:
                                ninit[k] = v
                    npending.append(ninit)

                pending = npending

            # expand permutations
            pending = list(reversed(pending))
            expanded = []
            while pending:
                perm = pending.pop()
                for k, v in sorted(perm.items()):
                    if not isinstance(v, str) and isinstance(v, abc.Iterable):
                        for nv in reversed(v):
                            nperm = perm.copy()
                            nperm[k] = nv
                            pending.append(nperm)
                        break
                else:
                    expanded.append(perm)

            # generate permutations
            case.perms = []
            for i, perm in enumerate(expanded):
                case.perms.append(case.permute(perm, permno=i, **args))

            # also track non-unique defines
            case.defines = {}
            for k, v in case.perms[0].defines.items():
                if all(perm.defines[k] == v for perm in case.perms):
                    case.defines[k] = v

        # track all perms and non-unique defines
        self.perms = []
        for case in self.cases:
            self.perms.extend(case.perms)

        self.defines = {}
        for k, v in self.perms[0].defines.items():
            if all(perm.defines[k] == v for perm in self.perms):
                self.defines[k] = v

        return self.perms

    def build(self, **args):
        # build test.c
        f = io.StringIO()
        f.write(GLOBALS)

        for case in self.cases:
            f.write('\n')
            case.build(f, **args)

        f.write('\n')
        f.write('int main(int argc, char **argv) {\n')
        f.write(4*' '+'int case_ = (argc == 3) ? atoi(argv[1]) : 0;\n')
        f.write(4*' '+'int perm = (argc == 3) ? atoi(argv[2]) : 0;\n')
        for perm in self.perms:
            f.write(4*' '+'if (argc != 3 || '
                '(case_ == %d && perm == %d)) { ' % (
                    perm.caseno, perm.permno))
            f.write('test_case%d(' % perm.caseno)
            first = True
            for k, v in sorted(perm.defines.items()):
                if k not in perm.case.defines:
                    if not first:
                        f.write(', ')
                    else:
                        first = False
                    f.write(str(v))
            f.write('); }\n')
        f.write('}\n')

        # add test-related rules
        rules = RULES.replace(4*' ', '\t')

        with open(self.path + '.test.mk', 'w') as mk:
            mk.write(rules)
            mk.write('\n')

            # add truely global defines globally
            for k, v in sorted(self.defines.items()):
                mk.write('%s: override CFLAGS += -D%s=%r\n' % (
                    self.path+'.test', k, v))

            # write test.c in base64 so make can decide when to rebuild
            mk.write('%s: %s\n' % (self.path+'.test.t.c', self.path))
            mk.write('\tbase64 -d <<< ')
            mk.write(base64.b64encode(
                f.getvalue().encode('utf8')).decode('utf8'))
            mk.write(' > $@\n')

        self.makefile = self.path + '.test.mk'
        self.target = self.path + '.test'
        return self.makefile, self.target

    def test(self, caseno=None, permno=None, **args):
        # run test suite!
        if not args.get('verbose', True):
            sys.stdout.write(self.name + ' ')
            sys.stdout.flush()
        for perm in self.perms:
            if caseno is not None and perm.caseno != caseno:
                continue
            if permno is not None and perm.permno != permno:
                continue

            try:
                perm.test(**args)
            except TestFailure as failure:
                if not args.get('verbose', True):
                    sys.stdout.write(FAIL)
                    sys.stdout.flush()
                if not args.get('keep_going', False):
                    if not args.get('verbose', True):
                        sys.stdout.write('\n')
                    raise
            else:
                if not args.get('verbose', True):
                    sys.stdout.write(PASS)
                    sys.stdout.flush()

        if not args.get('verbose', True):
            sys.stdout.write('\n')

def main(**args):
    testpath = args['testpath']

    # optional brackets for specific test
    m = re.search(r'\[(\d+)(?:,(\d+))?\]$', testpath)
    if m:
        caseno = int(m.group(1))
        permno = int(m.group(2)) if m.group(2) is not None else None
        testpath = testpath[:m.start()]
    else:
        caseno = None
        permno = None

    # figure out the suite's toml file
    if os.path.isdir(testpath):
        testpath = testpath + '/test_*.toml'
    elif os.path.isfile(testpath):
        testpath = testpath
    elif testpath.endswith('.toml'):
        testpath = TEST_DIR + '/' + testpath
    else:
        testpath = TEST_DIR + '/' + testpath + '.toml'

    # find tests
    suites = []
    for path in glob.glob(testpath):
        suites.append(TestSuite(path, **args))

    # sort for reproducability
    suites = sorted(suites)

    # generate permutations
    defines = {}
    for define in args['D']:
        k, v, *_ = define.split('=', 2) + ['']
        defines[k] = v

    for suite in suites:
        suite.permute(defines, **args)

    # build tests in parallel
    print('====== building ======')
    makefiles = []
    targets = []
    for suite in suites:
        makefile, target = suite.build(**args)
        makefiles.append(makefile)
        targets.append(target)

    cmd = (['make', '-f', 'Makefile'] +
        list(it.chain.from_iterable(['-f', m] for m in makefiles)) +
        ['CFLAGS+=-fdiagnostics-color=always'] +
        [target for target in targets])
    stdout = []
    if args.get('verbose', False):
        print(' '.join(cmd))
    proc = sp.Popen(cmd,
        universal_newlines=True,
        bufsize=1,
        stdout=sp.PIPE,
        stderr=sp.STDOUT)
    for line in iter(proc.stdout.readline, ''):
        stdout.append(line)
        if args.get('verbose', False):
            sys.stdout.write(line)
    proc.wait()

    if proc.returncode != 0:
        if not args.get('verbose', False):
            for line in stdout:
                sys.stdout.write(line)
        sys.exit(-3)

    print('built %d test suites, %d test cases, %d permutations' % (
        len(suites),
        sum(len(suite.cases) for suite in suites),
        sum(len(suite.perms) for suite in suites)))

    print('====== testing ======')
    try:
        for suite in suites:
            suite.test(caseno, permno, **args)
    except TestFailure:
        pass

    print('====== results ======')
    passed = 0
    failed = 0
    for suite in suites:
        for perm in suite.perms:
            if not hasattr(perm, 'result'):
                continue

            if perm.result == PASS:
                passed += 1
            else:
                sys.stdout.write("--- %s ---\n" % perm)
                if perm.result.assert_:
                    for line in perm.result.stdout[:-1]:
                        sys.stdout.write(line)
                    sys.stdout.write(
                        "\033[97m{path}:{lineno}:\033[91massert:\033[0m "
                        "{message}\n{line}\n".format(
                            **perm.result.assert_))
                else:
                    for line in perm.result.stdout:
                        sys.stdout.write(line)
                sys.stdout.write('\n')
                failed += 1

    print('tests passed: %d' % passed)
    print('tests failed: %d' % failed)

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(
        description="Run parameterized tests in various configurations.")
    parser.add_argument('testpath', nargs='?', default=TEST_DIR,
        help="Description of test(s) to run. By default, this is all tests \
            found in the \"{0}\" directory. Here, you can specify a different \
            directory of tests, a specific file, a suite by name, and even a \
            specific test case by adding brackets. For example \
            \"test_dirs[0]\" or \"{0}/test_dirs.toml[0]\".".format(TEST_DIR))
    parser.add_argument('-D', action='append', default=[],
        help="Overriding parameter definitions.")
    parser.add_argument('-v', '--verbose', action='store_true',
        help="Output everything that is happening.")
    parser.add_argument('-t', '--trace', action='store_true',
        help="Normally trace output is captured for internal usage, this \
            enables forwarding trace output which is usually too verbose to \
            be useful.")
    parser.add_argument('-k', '--keep-going', action='store_true',
        help="Run all tests instead of stopping on first error. Useful for CI.")
# TODO
#    parser.add_argument('--gdb', action='store_true',
#        help="Run tests under gdb. Useful for debugging failures.")
    parser.add_argument('--valgrind', action='store_true',
        help="Run non-leaky tests under valgrind to check for memory leaks. \
            Tests marked as \"leaky = true\" run normally.")
    main(**vars(parser.parse_args()))
