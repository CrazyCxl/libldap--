# libldap--
LDAP client library for C++

# install
cmake . 

make install

# use demo

    LDAPConnection *connect = new LDAPConnection("ldap://x.x.x.x");

    connect->SASLBind("cn=xxx,dc=xxx,dc=xxx","xxx");

    connect->SetResultSizeLimit(x);

    LDAPResult *result = connect->Search("cn=xxx,dc=xxx,dc=xxx",LDAP_SCOPE_BASE,"(cn=xxx)");

    delete result;
