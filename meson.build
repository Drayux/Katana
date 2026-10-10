project('katana', 'c',
    version: 'pre-release',
    meson_version: '>=1.1',
    default_options: [
        'c_std=gnu17',
        'buildtype=debug',
        'warning_level=1',
    ])

# TODO: Ensure the following are set by the warning_level ^^
# '-Wbad-function-cast',
# '-Wsign-compare',

add_project_arguments('-DAPP_VERSION="@0@"'.format(meson.project_version()), language: 'c')
add_project_arguments('-D_GNU_SOURCE=1 ', language: 'c')
add_project_arguments('-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_16', language: 'c')
add_project_arguments('-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_16', language: 'c')

# Strict warnings on release builds
if get_option('buildtype') == 'release'
	add_project_arguments('-Wpedantic', '-Werror', language: 'c')
    # add_project_arguments('-DLOG_LEVEL=1', language: 'c') # ERROR by default
else
    add_project_arguments('-DDEBUG', language: 'c')
    add_project_arguments('-DLOG_LEVEL=0', language: 'c')
endif

# Check for IOCTL map support (else fallback to reading /proc/)
cc = meson.get_compiler('c')
has_procmap_ioctl = cc.has_header_symbol('linux/fs.h', 'PROCMAP_QUERY')
ioctl_maps = get_option('ioctl_maps').enable_auto_if(has_procmap_ioctl)
if ioctl_maps.enabled()
    add_project_arguments('-DIOCTL_MAPS', language: 'c')
endif

extra_c_flags = [
    '-DPREFIX="' + get_option('prefix') + '"',
    '-DDATADIR="' + get_option('datadir') + '"',
]

subdir('lib')
subdir('timer')
subdir('ctl')
# TODO: subdir('plugins')


# -- INSTALLATION --

message('prefix: ' + get_option('prefix')) # /usr/local by default
message('datadir: ' + get_option('datadir')) # share by default
message('buildtype: ' + get_option('buildtype'))
message('ioctl_maps: ' + ioctl_maps.enabled().to_string())

install_data(
    'assets/org.libresplit.LibreSplit.desktop',
    install_dir: get_option('prefix') / get_option('datadir') / 'applications',
)
install_data(
    'README.md',
    install_dir: get_option('prefix') / get_option('datadir') / 'doc' / meson.project_name(),
)
install_data(
    'LICENSE.md',
    install_dir: get_option('prefix') / get_option('datadir') / 'licenses' / meson.project_name(),
)

# TODO: better install dir for this to make desktop and gtk icons work on debug builds/uninstalled tests
foreach size : [16, 22, 24, 32, 36, 48, 64, 72, 96, 128, 256, 512, 1024]
    install_data(
        'assets/icons/libresplit-@0@.png'.format(size),
        install_dir: get_option('prefix') / get_option('datadir') / 'icons/hicolor/@0@x@0@/apps/'.format(size),
        rename: 'libresplit.png',
        install_mode: 'rw-r--r--',
    )
endforeach
install_data(
    'assets/libresplit.svg',
    install_dir: get_option('prefix') / get_option('datadir') / 'icons/hicolor/scalable/apps/',
    rename: 'libresplit.svg',
    install_mode: 'rw-r--r--',
)

#gnome.post_install(update_desktop_database: true, gtk_update_icon_cache: true)
gnome.post_install(gtk_update_icon_cache: true)


# -- TESTS --

# Code formatting test
clang_format = find_program('clang-format', required: false)
if clang_format.found()
    # Check LibreSplit
    test(
        'clang-format-timer',
        clang_format,
        args: [
            '--dry-run',
            '--Werror', sources,
        ],
        suite: 'format',
    )
    # Check LibreSplitCTL
    test(
        'clang-format-ctl',
        clang_format,
        args: [
            '--dry-run',
            '--Werror', 'src/ctl.h', 'src/ctl.c',
        ],
        suite: 'format',
    )
else
    message('clang-format not found, skipping formatting test')
endif

# Static analysis + Linting
cppcheck = find_program('cppcheck', required: false)
if cppcheck.found()
    # Set args for analysing both sources.
    cppcheck_base_args = [
        '--std=c17',
        '--language=c',
        '--enable=warning,performance',
        '--error-exitcode=1',
        # Recommended silences for GTK/GLib apps
        '--library=gtk.cfg',
        '--suppress=missingIncludeSystem',
        '--suppress=unknownMacro',
        '--suppress=preprocessorErrorDirective',
        '--suppress=unusedParameter',
    ]
    # Check LibreSplit
    test(
        'cppcheck-timer',
        cppcheck,
        args: cppcheck_base_args + sources,
        suite: 'lint',
    )
    # Check LibreSplitCTL
    test(
        'cppcheck-ctl',
        cppcheck,
        args: cppcheck_base_args + 'src/ctl.h src/ctl.c',
        suite: 'lint',
    )
else
    message('cppcheck not found, skipping linting test')
endif
