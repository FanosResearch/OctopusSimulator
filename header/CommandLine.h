/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2008 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#ifndef COMMAND_LINE_H
#define COMMAND_LINE_H

#include <string>
#include <sstream>
#include <vector>


/**
 * \file
 * \ingroup commandline
 * ns3::CommandLine declaration.
 */

namespace octopus {

class CommandLine
{
public:
  /** Constructor */
  CommandLine ();
  /**
   * Copy constructor
   *
   * \param [in] cmd The CommandLine to copy from
   */
  CommandLine (const CommandLine &cmd);
  /**
   * Assignment
   *
   * \param [in] cmd The CommandLine to assign from
   * \return The CommandLine
   */
  CommandLine &operator = (const CommandLine &cmd);
  /** Destructor */
  ~CommandLine ();

  /**
   * Supply the program usage and documentation.
   *
   * \param [in] usage Program usage message to write with \c --help.
   */
  void Usage (const std::string usage);
  
  /**
   * Add a program argument, assigning to POD
   *
   * \param [in] name The name of the program-supplied argument
   * \param [in] help The help text used by \c \-\-PrintHelp
   * \param [out] value A reference to the variable where the
   *        value parsed will be stored (if no value
   *        is parsed, this variable is not modified).
   */
  template <typename T>
  void AddValue (const std::string &name,
                 const std::string &help,
                 T &value);

  /**
   * Add a program argument as a shorthand for an Attribute.
   *
   * \param [in] name The name of the program-supplied argument.
   * \param [out] attributePath The fully-qualified name of the Attribute
   */
  void AddValue (const std::string &name,
                 const std::string &attributePath);
  
  /**
   * Add a non-option argument, assigning to POD
   *
   * \param [in] name The name of the program-supplied argument
   * \param [in] help The help text used by \c \-\-PrintHelp
   * \param [out] value A reference to the variable where the
   *        value parsed will be stored (if no value
   *        is parsed, this variable is not modified).
   */
  template <typename T>
  void AddNonOption (const std::string name, const std::string help, T & value);

  /**
   * Get extra non-option arguments by index.
   * This allows CommandLine to accept more non-option arguments than
   * have been configured explicitly with AddNonOption().
   *
   * This is only valid after calling Parse().
   *
   * \param [in] i The index of the non-option argument to return.
   * \return The i'th non-option argument, as a string.
   */
  std::string GetExtraNonOption (std::size_t i) const;
  
  /**
   * Get the total number of non-option arguments found,
   * including those configured with AddNonOption() and extra non-option
   * arguments.
   *
   * This is only valid after calling Parse().
   *
   * \returns the number of non-option arguments found.
   */
  std::size_t GetNExtraNonOptions (void) const;

  /**
   * Parse the program arguments
   *
   * \param [in] argc The 'argc' variable: number of arguments (including the
   *        main program name as first element).
   * \param [in] argv The 'argv' variable: a null-terminated array of strings,
   *        each of which identifies a command-line argument.
   * 
   * Obviously, this method will parse the input command-line arguments and
   * will attempt to handle them all.
   *
   * As a side effect, this method saves the program basename, which
   * can be retrieved by GetName().
   */
  void Parse (int argc, char *argv[]);

  /**
   * Parse the program arguments.
   *
   * This version may be convenient when synthesizing arguments
   * programmatically.  Other than the type of argument this behaves
   * identically to Parse(int, char *)
   *
   * \param [in] args The vector of arguments.
   */
  void Parse (std::vector<std::string> args);

  /**
   * Get the program name
   *
   * \return The program name.  Only valid after calling Parse()
   */
  std::string GetName () const;

  /**
   * \brief Print program usage to the desired output stream
   *
   * Handler for \c \-\-PrintHelp and \c \-\-help:  print Usage(), argument names, and help strings
   *
   * Alternatively, an overloaded operator << can be used:
   * \code
   *       CommandLine cmd;
   *       cmd.Parse (argc, argv);
   *     ...
   *
   *       std::cerr << cmd;
   * \endcode
   *
   * \param [in,out] os The output stream to print on.
   */
  void PrintHelp (std::ostream &os) const;

private:

  /**
   * \ingroup commandline
   * \brief The argument abstract base class
   */
  class Item 
  {
  public:
    std::string m_name;       /**< Argument label:  \c \-\--m_name=... */
    std::string m_help;       /**< Argument help string */
    virtual ~Item ();         /**< Destructor */
    /**
     * Parse from a string.
     *
     * \param [in] value The string representation
     * \return \c true if parsing the value succeeded
     */
    virtual bool Parse (const std::string value) = 0;
    /**
     * \return \c true if this item has a default value.
     */
    virtual bool HasDefault () const;
    /**
     * \return The default value
     */
    virtual std::string GetDefault () const;
  };  // class Item

  /**
   * \ingroup commandline
   *\brief An argument Item assigning to POD
   */
  template <typename T>
  class UserItem : public Item
  {
  public:
    // Inherited
    virtual bool Parse (const std::string value);
    bool HasDefault () const;
    std::string GetDefault () const;
      
    T *m_valuePtr;            /**< Pointer to the POD location */
    std::string m_default;    /**< String representation of default value */
  };  // class UserItem

  class StringItem : public Item
  {
  public:
    // Inherited
    bool Parse (const std::string value);
    bool HasDefault (void) const;
    std::string GetDefault (void) const;
    
    std::string m_value;     /**< The argument value. */
  };  // class StringItem

  // /**
  //  * \ingroup commandline
  //  * \brief An argument Item using a Callback to parse the input
  //  */
  // class CallbackItem : public Item
  // {
  // public:
  //   /**
  //    * Parse from a string.
  //    *
  //    * \param [in] value The string representation
  //    * \return \c true if parsing the value succeeded
  //    */
  //   virtual bool Parse (const std::string value);
  //   Callback<bool, std::string> m_callback;  /**< The Callback */
  // };  // class CallbackItem


  /**
   * Handle an option in the form \c param=value.
   *
   * \param [in] param The option string. 
   * \returns \c true if this was really an option.
   */
  bool HandleOption (const std::string & param) const;
  
  // /**
  //  * Handle a non-option
  //  *
  //  * \param [in] value The command line non-option value.
  //  * \return \c true if \c value could be parsed correctly.
  //  */
  // bool HandleNonOption (const std::string &value);
  
  /**
   * Match name against the program or general arguments,
   * and dispatch to the appropriate handler.
   *
   * \param [in] name The argument name
   * \param [in] value The command line value
   */
  void HandleArgument (const std::string &name, const std::string &value) const;
  /**
   * Callback function to handle attributes.
   *
   * \param [in] name The full name of the Attribute.
   * \param [in] value The value to assign to \p name.
   * \return \c true if the value was set successfully, false otherwise.
   */  
  static bool HandleAttribute (const std::string name, const std::string value);

  /**
   * Handler for \c \-\-PrintGlobals:  print all global variables and values
   * \param [in,out] os The output stream to print on.
   */
  void PrintGlobals (std::ostream &os) const;
  /**
   * Handler for \c \-\-PrintAttributes:  print the attributes for a given type.
   *
   * \param [in,out] os the output stream.
   * \param [in] type The TypeId whose Attributes should be displayed
   */
  void PrintAttributes (std::ostream &os, const std::string &type) const;
  /**
   * Handler for \c \-\-PrintGroup:  print all types belonging to a given group.
   *
   * \param [in,out] os The output stream.
   * \param [in] group The name of the TypeId group to display
   */
  void PrintGroup (std::ostream &os, const std::string &group) const;
  /**
   * Handler for \c \-\-PrintTypeIds:  print all TypeId names.
   *
   * \param [in,out] os The output stream.
   */
  void PrintTypeIds (std::ostream &os) const;
  /**
   * Handler for \c \-\-PrintGroups:  print all TypeId group names
   *
   * \param [in,out] os The output stream.
   */
  void PrintGroups (std::ostream &os) const;
  /**
   * Copy constructor
   *
   * \param [in] cmd CommandLine to copy
   */
  void Copy (const CommandLine &cmd);
  /** Remove all arguments, Usage(), name */
  void Clear (void);

  typedef std::vector<Item *> Items;    /**< Argument list container */
  Items m_options;                      /**< The list of option arguments */
  Items m_nonOptions;                   /**< The list of non-option arguments */
  std::size_t m_NNonOptions;            /**< The expected number of non-option arguments */
  std::size_t m_nonOptionCount;         /**< The number of actual non-option arguments seen so far. */
  std::string m_usage;                  /**< The Usage string */
  std::string m_name;                   /**< The program name */

};  // class CommandLine


/** \ingroup commandline
 *  \defgroup commandlinehelper Helpers to Specialize on bool
 */
/**
 * \ingroup commandlinehelper
 * \brief Helpers for CommandLine to specialize on bool
 */
namespace CommandLineHelper {

  /**
   * \ingroup commandlinehelper
   * \brief Helpers to specialize CommandLine::UserItem::Parse() on bool
   *
   * \param [in] value The argument name
   * \param [out] val The argument location
   * \return \c true if parsing was successful
   * @{
   */
  template <typename T>
  bool UserItemParse (const std::string value, T & val);
  template <>
  bool UserItemParse<bool> (const std::string value, bool & val);
  /**@}*/

  /**
   * \ingroup commandlinehelper
   * \brief Helper to specialize CommandLine::UserItem::GetDefault() on bool
   *
   * \param [in] val The argument value
   * \return The string representation of value
   * @{
   */
  template <typename T>
  std::string GetDefault (const T & val);
  template <>
  std::string GetDefault<bool> (const bool & val);
  /**@}*/

}  // namespace CommandLineHelper
    
  
  
} // namespace octopus


/********************************************************************
 *  Implementation of the templates declared above.
 ********************************************************************/

namespace octopus {

template <typename T>
void 
CommandLine::AddValue (const std::string &name,
                       const std::string &help,
                       T &value)
{
  UserItem<T> *item = new UserItem<T> ();
  item->m_name = name;
  item->m_help = help;
  item->m_valuePtr = &value;
  
  std::stringstream ss;
  ss << value;
  ss >> item->m_default;
    
  m_options.push_back (item);
}

template <typename T>
void
CommandLine::AddNonOption (const std::string name,
                           const std::string help,
                           T & value)
{
  UserItem<T> *item = new UserItem<T> ();
  item->m_name = name;
  item->m_help = help;
  item->m_valuePtr = &value;
  
  std::stringstream ss;
  ss << value;
  ss >> item->m_default;
  m_nonOptions.push_back (item);
  ++m_NNonOptions;
    
}

template <typename T>
bool
CommandLine::UserItem<T>::HasDefault () const
{
  return true;
}

template <typename T>
std::string
CommandLine::UserItem<T>::GetDefault () const
{
  return CommandLineHelper::GetDefault<T> (*m_valuePtr);
}

template <typename T>
std::string
CommandLineHelper::GetDefault (const T & val)
{
  std::ostringstream oss;
  oss << val;
  return oss.str ();
}


template <typename T>
bool
CommandLine::UserItem<T>::Parse (const std::string value)
{
  return CommandLineHelper::UserItemParse<T> (value, *m_valuePtr);
}

template <typename T>
bool
CommandLineHelper::UserItemParse (const std::string value, T & val)
{
  std::istringstream iss;
  iss.str (value);
  iss >> val;
  return !iss.bad () && !iss.fail ();
}

/**
 * Overloaded operator << to print program usage
 * (shortcut for CommandLine::PrintHelper)
 *
 * \see CommandLine::PrintHelper
 *
 * Example usage:
 * \code
 *    CommandLine cmd;
 *    cmd.Parse (argc, argv);
 *    ...
 *    
 *    std::cerr << cmd;
 * \endcode
 *
 * \param [in,out] os The stream to print on.
 * \param [in] cmd The CommandLine describing the program.
 * \returns The stream.
 */
std::ostream & operator << (std::ostream & os, const CommandLine & cmd);

} // namespace octopus

#endif /* COMMAND_LINE_H */
