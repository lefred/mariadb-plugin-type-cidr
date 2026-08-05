package My::Suite::TypeCidr;

@ISA = qw(My::Suite);

return "Not run for embedded server" if $::opt_embedded_server;
return "No TYPE_CIDR plugin" unless $ENV{TYPE_CIDR_SO};
bless { };
