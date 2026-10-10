# libkatana declared in <repo>/lib/meson.build
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

gnome = import('gnome')
resources = gnome.compile_resources(
    'libresplit-resources',
    '../assets/libresplit.gresource.xml',
    source_dir: '../assets/resources',
    c_name: 'katana',
)

sources = files(
    'gui/actions.c',
    'gui/app_window.c',
    'gui/backends/x11.c',

    # Components
    'gui/component/best-sum.c',
    'gui/component/components.c',
    'gui/component/detailed-timer.c',
    'gui/component/pb.c',
    'gui/component/prev-segment.c',
    'gui/component/splits.c',
    'gui/component/title.c',
    'gui/component/wr.c',
    'gui/context_menu.c',
    'gui/dialogs.c',
    'gui/game.c',
    'gui/theming.c',
    'gui/timer.c',

    # GUI
    'gui/folders.c',
    'gui/utils.c',
    'gui/welcome_box.c',
    'gui/widgets/alert.c',
    'gui/widgets/dialog.c',
    'gui/widgets/help_dialog.c',
    'gui/widgets/settings_dialog.c',
    'keybinds/bind.c',

    # Keybinds
    'keybinds/keybinds.c',
    'keybinds/keybinds_callbacks.c',

    # Plugins
    'plugins/plugin_loading.c',
    'plugins/plugin_utils.c',


    # LASR
    'lasr/auto-splitter.c',
    'lasr/export.c',
    'lasr/functions/bitwise.c',
    'lasr/functions/getBaseAddress.c',
    'lasr/functions/getMaps.c',
    'lasr/functions/getModuleSize.c',
    'lasr/functions/getPID.c',
    'lasr/functions/md5.c',
    'lasr/functions/print_tbl.c',
    'lasr/functions/process.c',
    'lasr/functions/readAddress.c',
    'lasr/functions/shallow_copy_tbl.c',
    'lasr/functions/signature.c',
    'lasr/functions/sizeOf.c',
    'lasr/functions/strtoida.c',
    'lasr/maps/maps.c',
    'lasr/memory_iter/memory_iterator.c',
    'lasr/settings.c',
    'lasr/utils.c',
    'main.c',
    'runs.c',
    'server.c',
    'timer.c',

    # Settings
    'settings/definitions.c',
    'settings/settings.c',
)

# The main timer program
executable(
    'katana',
    sources,
    resources,
    dependencies: [ libkatana, threads, gtk, gtk_x11, luajit, x11, jansson, openssl ],
    c_args: extra_c_flags,
    install: true,
)
