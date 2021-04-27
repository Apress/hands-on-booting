BEGIN{
        print "const char *dns_type_to_string(int type) {\n\tswitch(type) {"
}
{
        printf "        case DNS_TYPE_%s: return ", $1;
        sub(/_/, "-");
        printf "\"%s\";\n", $1
}
END{
        print "        default: return NULL;\n\t}\n}\n"
}
