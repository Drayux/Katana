project('katana', 'c',
    version: 'pre-release',
    default_options: ['c_std=gnu17'],
    meson_version: '>=1.1',
    default_options: [
        'buildtype=debug',
        'warning_level=1'
    ])

if get_option('buildtype') == 'release'
	add_project_arguments('-Wpedantic', '-Werror', language: 'c')
    add_project_arguments('-DLOG_LEVEL=1', language: 'c')
else
    add_project_arguments('-DDEBUG', language: 'c')
    add_project_arguments('-DLOG_LEVEL=0', language: 'c')
endif

add_project_arguments('-DAPP_VERSION="@0@"'.format(meson.project_version()), language: 'c')
add_project_arguments('-D_GNU_SOURCE=1 ', language: 'c')
add_project_arguments('-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_16', language: 'c')
add_project_arguments('-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_16', language: 'c')

cc = meson.get_compiler('c')
has_procmap_ioctl = cc.has_header_symbol('linux/fs.h', 'PROCMAP_QUERY')
ioctl_maps = get_option('ioctl_maps').enable_auto_if(has_procmap_ioctl)
if ioctl_maps.enabled()
    add_project_arguments('-DIOCTL_MAPS', language: 'c')
endif

threads = dependency('threads')
luajit = dependency('luajit')
x11 = dependency('x11')
openssl = dependency('openssl')
# TODO: These should become optional at some point
gtk = dependency('gtk4', version: '>= 4.16', include_type: 'system')
gtk_x11 = dependency('gtk4-x11', version: '>= 4.16', include_type: 'system')
### (and this one might become a plugin dependency not main project)
jansson = dependency('jansson')
###

public_api = include_directories('include')
private_headers = include_directories('src')

sources = files(
    'src/gui/actions.c',
    'src/gui/app_window.c',
    'src/gui/backends/x11.c',

    # Components
    'src/gui/component/best-sum.c',
    'src/gui/component/components.c',
    'src/gui/component/detailed-timer.c',
    'src/gui/component/pb.c',
    'src/gui/component/prev-segment.c',
    'src/gui/component/splits.c',
    'src/gui/component/title.c',
    'src/gui/component/wr.c',
    'src/gui/context_menu.c',
    'src/gui/dialogs.c',
    'src/gui/game.c',
    'src/gui/theming.c',
    'src/gui/timer.c',

    # GUI
    'src/gui/folders.c',
    'src/gui/utils.c',
    'src/gui/welcome_box.c',
    'src/gui/widgets/alert.c',
    'src/gui/widgets/dialog.c',
    'src/gui/widgets/help_dialog.c',
    'src/gui/widgets/settings_dialog.c',
    'src/keybinds/bind.c',

    # Keybinds
    'src/keybinds/keybinds.c',
    'src/keybinds/keybinds_callbacks.c',

    # Plugins
    'src/plugins/plugin_loading.c',
    'src/plugins/plugin_utils.c',


    # LASR
    'src/lasr/auto-splitter.c',
    'src/lasr/export.c',
    'src/lasr/functions/bitwise.c',
    'src/lasr/functions/getBaseAddress.c',
    'src/lasr/functions/getMaps.c',
    'src/lasr/functions/getModuleSize.c',
    'src/lasr/functions/getPID.c',
    'src/lasr/functions/md5.c',
    'src/lasr/functions/print_tbl.c',
    'src/lasr/functions/process.c',
    'src/lasr/functions/readAddress.c',
    'src/lasr/functions/shallow_copy_tbl.c',
    'src/lasr/functions/signature.c',
    'src/lasr/functions/sizeOf.c',
    'src/lasr/functions/strtoida.c',
    'src/lasr/maps/maps.c',
    'src/lasr/memory_iter/memory_iterator.c',
    'src/lasr/settings.c',
    'src/lasr/utils.c',
    'src/logging.c',
    'src/main.c',
    'src/runs.c',
    'src/server.c',

    # Settings
    'src/settings/definitions.c',
    'src/settings/settings.c',
    'src/settings/utils.c',
    'src/shared.c',
    'src/timer.c',
)

ctl_sources = files(
    'src/ctl.c',
    'src/shared.c',
)

gnome = import('gnome')
resources = gnome.compile_resources(
    'libresplit-resources',
    'assets/libresplit.gresource.xml',
    source_dir: 'assets/resources',
    c_name: 'katana',
)

shared_c_flags = [
    '-DPREFIX="' + get_option('prefix') + '"',
    '-DDATADIR="' + get_option('datadir') + '"',
    '-Wbad-function-cast',
    '-Wsign-compare',
]

executable(
    'katana',
    sources,
    resources,
    dependencies: [threads, gtk, gtk_x11, luajit, x11, jansson, openssl],
    c_args: shared_c_flags,
    install: true,
    include_directories: [public_api, private_headers],
)

executable(
    'katana-ctl',
    ctl_sources,
    c_args: shared_c_flags,
    install: true,
    include_directories: [public_api, private_headers],
)

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

# Code formatting test
clang_format = find_program('clang-format', required: false)
if clang_format.found()
    # Check LibreSplit
    test(
        'clang-format-libresplit',
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
            '--Werror', ctl_sources,
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
        'cppcheck-libresplit',
        cppcheck,
        args: cppcheck_base_args + sources,
        suite: 'lint',
    )
    # Check LibreSplitCTL
    test(
        'cppcheck-ctl',
        cppcheck,
        args: cppcheck_base_args + ctl_sources,
        suite: 'lint',
    )
else
    message('cppcheck not found, skipping linting test')
endif

#gnome.post_install(update_desktop_database: true, gtk_update_icon_cache: true)
gnome.post_install(gtk_update_icon_cache: true)
