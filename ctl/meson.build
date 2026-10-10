# libkatana declared in <repo>/lib/meson.build

gnome = import('gnome')
resources = gnome.compile_resources(
    'libresplit-resources',
    '../assets/libresplit.gresource.xml',
    source_dir: '../assets/resources',
    c_name: 'katana',
)

executable(
    'katana-ctl',
    files('main.c'),
    dependencies: [ libkatana ],
    c_args: extra_c_flags,
    install: true,
)
