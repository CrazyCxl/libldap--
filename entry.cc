#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <memory>
#include "ldap++.h"
#include <ldap.h>
#ifdef HAVE_LDIF_H
#include <ldif.h>
#endif

namespace ldap_client
{
static const std::string k_NewItemsString =
	"All items in this file are new.";

/**
 * Create an entirely new LDAP entry.
 *
 * @param conn The LDAP connection the record should be synchronized over.
 * @param dn   The Distinguished Name field of the new record.
 */
LDAPEntry::LDAPEntry(LDAPConnection* conn, std::string dn)
: _conn(conn), _dn(dn)
{
	_isnew = true;
}

/**
 * Used by LDAPConnection to create a new object corresponding to an
 * LDAP record.
 *
 * @param conn  The LDAP connection this record originated from.
 * @param entry The LDAPMessage struct containing the entry's data.
 */
LDAPEntry::LDAPEntry(LDAPConnection* conn, LDAPMessage* entry)
: _conn(conn)
{
	char *attr = ldap_get_dn(_conn->_ldap, entry);
	BerElement *ptr;

	_isnew = false;

	_dn = std::string(attr);
	ldap_memfree(attr);

	for (attr = ldap_first_attribute(_conn->_ldap, entry, &ptr);
		attr != 0; attr = ldap_next_attribute(_conn->_ldap, entry, ptr))
	{
        SearchableVector<std::string> values;
		struct berval **bv =
				ldap_get_values_len(_conn->_ldap, entry, attr);

		for (int i = 0; i < ldap_count_values_len(bv); i++)
            values.push_back(std::string(bv[i]->bv_val, bv[i]->bv_len));

		ldap_value_free_len(bv);

		_data[std::string(attr)] = values;
		ldap_memfree(attr);
	}

	if (ptr != 0)
		ber_free(ptr, 0);
}

/**
 * Return the DN field of the record.
 *
 * @return String containing the DN field.
 */
std::string LDAPEntry::GetDN()
{
	return _dn;
}

/**
 * Returns the name of all attributes in the LDAP record, e.g.
 * "cn", "emailAdress", etc.
 *
 * @return Pointer to a vector of strings with the attribute names.
 */
SearchableVector<std::string> LDAPEntry::GetKeys()
{
    SearchableVector<std::string> rv;

    for (auto iter = _data.begin(); iter != _data.end(); iter++)
        rv.push_back((*iter).first);

	return rv;
}

/**
 * Returns all values for a given LDAP attribute.
 *
 * @param attribute Name of the LDAP attribute.
 * @return Pointer to a vector of strings with the values.
 */
SearchableVector<std::string> LDAPEntry::GetValue(std::string attribute)
{
	return _data[attribute];
}

/**
 * Returns the first value of the given LDAP attribute.
 *
 * @param attribute Name of the LDAP attribute.
 * @return String containing the attribute value.
 */
std::string LDAPEntry::GetFirstValue(std::string attribute)
{
	if (_data.find(attribute) == _data.end())
		return std::string("");

    return _data[attribute].front();
}

/**
 * Adds a value to the given attribute. If the attribute wasn't set yet it
 * will be created. This will only be written to LDAP when the Sync()
 * method is invoked.
 *
 * @param attribute LDAP attribute to add the value to.
 * @param value     Value to add to the attribute.
 */
void LDAPEntry::AddValue(std::string attribute, std::string value)
{

    _data[attribute].push_back(value);

    if (_added[attribute] == 0)
        _added[attribute] = new SearchableVector<std::string>;

    _added[attribute]->push_back(value);
}

/**
 * Remove the given value from the LDAP attribute.
 *
 * @param attribute Attribute to remove the value from.
 * @param value     The specific value to remove.
 */
void LDAPEntry::RemoveValue(std::string attribute, std::string value)
{
	// Bail out early if the key isn't in the record.
    if (_data.count(attribute) == 0)
		return;

    for (auto iter = _data[attribute].begin(); iter != _data[attribute].end(); iter++)
		if (!(*iter).compare(value))
		{
			if (_removed[attribute] == 0)
				_removed[attribute] = new SearchableVector<std::string>;

			_removed[attribute]->push_back(value);
		}

    if (_data[attribute].empty())
		_data.erase(attribute);
}

/**
 * Remove all values from the LDAP attribute.
 *
 * @param attribute Attribute to remove the values from.
 */
void LDAPEntry::RemoveAllValues(std::string attribute)
{
	SearchableVector<std::string>::iterator iter;

	// Bail out early if the key isn't in the record.
    if (_data.count(attribute) == 0)
		return;

    for (iter = _data[attribute].begin(); iter != _data[attribute].end(); iter++)
	{
		if (_removed[attribute] == 0)
			_removed[attribute] = new SearchableVector<std::string>;

		_removed[attribute]->push_back((*iter));
	}

	_data.erase(attribute);
}

/**
 * Write changes to LDAP. If the entry wasn't in LDAP yet, it will be
 * created. Changes are executed in the order: removals, additions.
 *
 * @exception LDAPException Error occurred writing data to LDAP.
 */
void LDAPEntry::Sync()
{
	// TODO(tonnerre): the bookkeeping in this method is terrible.
	std::map<std::string, SearchableVector<std::string>*>::iterator iter;
	SearchableVector<std::string>::iterator v_iter;
	std::vector<char*> cleanup;
	std::vector<char*>::iterator c_iter;
	std::vector<std::vector<char*>* > v_cleanup;
	std::vector<std::vector<char*>* >::iterator vc_iter;
	std::vector<LDAPMod*> mods;
	std::vector<LDAPMod*>::iterator m_iter;
	int rc;

	for (iter = _removed.begin(); iter != _removed.end(); iter++)
	{
		LDAPMod* mod = new LDAPMod;
		std::vector<char*>* values = new std::vector<char*>;
		char* newval;

		for (v_iter = _removed[iter->first]->begin();
				v_iter != _removed[iter->first]->end();
				v_iter++)
		{
			newval = strdup(v_iter->c_str());
			cleanup.push_back(newval);
			values->push_back(newval);
		}

		// NULL terminate values.
		values->push_back(0);

		newval = strdup(iter->first.c_str());
		cleanup.push_back(newval);
		v_cleanup.push_back(values);

		mod->mod_op = LDAP_MOD_DELETE;
		mod->mod_type = newval;
		mod->mod_vals.modv_strvals = &(*values)[0];

		mods.push_back(mod);
	}

	for (iter = _added.begin(); iter != _added.end(); iter++)
	{
		LDAPMod* mod = new LDAPMod;
		std::vector<char*>* values = new std::vector<char*>;
		char* newval;

		for (v_iter = _added[iter->first]->begin();
				v_iter != _added[iter->first]->end();
				v_iter++)
		{
			newval = strdup(v_iter->c_str());
			cleanup.push_back(newval);
			values->push_back(newval);
		}

		// NULL terminate values.
		values->push_back(0);

		newval = strdup(iter->first.c_str());
		cleanup.push_back(newval);
		v_cleanup.push_back(values);

		mod->mod_op = LDAP_MOD_ADD;
		mod->mod_type = newval;
		mod->mod_vals.modv_strvals = &(*values)[0];

		mods.push_back(mod);
	}

	// This needs to be NULL terminated.
	mods.push_back(0);

	if (_isnew)
		rc = ldap_add_ext_s(_conn->_ldap, _dn.c_str(), &mods[0], 0, 0);
	else
		rc = ldap_modify_ext_s(_conn->_ldap, _dn.c_str(), &mods[0], 0, 0);

	for (m_iter = mods.begin(); m_iter != mods.end(); m_iter++)
		if (*m_iter)
			delete *m_iter;

	for (vc_iter = v_cleanup.begin(); vc_iter != v_cleanup.end(); vc_iter++)
		delete (*vc_iter);

	for (c_iter = cleanup.begin(); c_iter != cleanup.end(); c_iter++)
		free(*c_iter);

	if (rc != LDAP_SUCCESS)
		LDAPErrCode2Exception(_conn->_ldap, rc);
}

/**
 * Write the object to the given output stream in LDIF format.
 */
void LDAPEntry::Output(std::ostream& out)
{
#ifdef HAVE_LDIF_H
	BerElement* ber = 0;
	struct berval bv;
	struct berval* bvals;
	struct berval** bvp;
	char* ldif;

	if ((ldif = ldif_put_wrap(LDIF_PUT_VALUE, "dn", _dn.c_str(),
			_dn.length(), LDIF_LINE_WIDTH)))
	{
		out << ldif;
		ber_memfree(ldif);
	}

	if (_data.empty())
	{
		std::map<std::string, SearchableVector<std::string>*>::iterator iter;

		if ((ldif = ldif_put_wrap(LDIF_PUT_COMMENT, 0, k_NewItemsString.c_str(),
				k_NewItemsString.length(), LDIF_LINE_WIDTH)))
		{
			out << ldif;
			ber_memfree(ldif);
		}

		for (iter = _added.begin(); iter != _added.end(); iter++)
		{
			std::vector<std::string>::iterator v_iter;

			for (v_iter = iter->second->begin();
					v_iter != iter->second->end(); v_iter++)
				if ((ldif = ldif_put_wrap(LDIF_PUT_VALUE,
						iter->first.c_str(), v_iter->c_str(),
						v_iter->length(), LDIF_LINE_WIDTH)))
				{
					out << ldif;
					ber_memfree(ldif);
				}
		}
	}
	else
	{
		std::map<std::string, SearchableVector<std::string>*>::iterator iter;

		for (iter = _data.begin(); iter != _data.end(); iter++)
		{
			std::vector<std::string>::iterator v_iter;

			for (v_iter = iter->second->begin();
					v_iter != iter->second->end(); v_iter++)
			{
				if ((ldif = ldif_put_wrap(LDIF_PUT_VALUE, iter->first.c_str(),
						v_iter->c_str(), v_iter->length(), LDIF_LINE_WIDTH)))
				{
					out << ldif;
					ber_memfree(ldif);
				}
			}
		}
	}
#endif /* HAVE_LDIF_H */
}

}
