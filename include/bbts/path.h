/***************************************************************************
 *
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 *
 **************************************************************************/

/*
 * @file   path.h
 *
 * @author liuming03
 * @date   2013-4-9
 * @brief  关于文件路径的一些操作集
 */

#ifndef OP_OPED_NOAH_TOOLS_BBTS_AGENT_PATH_H
#define OP_OPED_NOAH_TOOLS_BBTS_AGENT_PATH_H

#include <string>

#include <boost/function.hpp>

struct stat;
struct FTW;

namespace bbts {

class Path {
public:
    typedef boost::function<int (const char* path, const struct ::stat* statbuf,
            int type, struct ::FTW* ftw_info)> NftwCallback;

    /**
     * @brief  去掉path末尾多余的斜杠'/'
     *
     * @param  path(in) 待过滤的路径
     * @return 返回过滤末尾'/'后的path
     */
    static std::string trim_back_slash(const std::string &path);

    /**
     * @brief  去掉path头部多余的斜杠'/'
     *
     * @param  path(in) 待过滤的路径
     * @return 返回过滤头部'/'后的path
     */
    static std::string trim_front_slash(const std::string &path);

    /**
     * @brief 分离给定path中父路径和最后一级路径名
     *
     * @param  path(in)         给定的路径
     * @param  parent_path(out) path的父路径
     * @param  short_name (out) path的最后一级路径
     * @return 成功true，失败false
     */
    static bool slipt(const std::string &path, std::string *parent_dir, std::string *name);

    /**
     * @brief  取得指定目录的父目录
     *
     * @param  path(in)  给定目录
     * @return 存在父目录则返回父目录，否则返回空串
     */
    static std::string parent_dir(const std::string &path);
    /**
     * @brief  取得某个路径的最后一级路径名
     *
     * @param  path(in) 源文件路径
     * @return 返回最后一级路径名
     */
    static std::string short_name(const std::string &path);

    /**
     * @brief  取得某个路径的子路径，即subpath("abc/def") == "def"
     *
     * @param  path(in) 源文件路径
     * @return 返回子路径名
     */
    static std::string subpath(const std::string &path);

    static std::string current_working_dir();

    static std::string home_dir();

    /**
     * @brief  补全为绝对路径
     *
     * @param  path(in) 某个给定路径
     * @return 转换为绝对路径，即如果是相对目录，则用当前工作目录补全
     */
    static std::string absolute(const std::string &path);

    /**
     * @brief  给路径中指定字符前插入反斜杠 
     * 以适应正则表达式, 如/a/b/c转换成\/a\/b\/c, \?, \+ 
     *
     * C字符串反斜杠会被转移，所以实际上是代码中加了两个反斜杠
     * @param  path(in) 某个路径
     * @return 转换后的path
     */

    static std::string regex_replace(const std::string &path, const std::string &chars);

    /**
     * @brief  判断给定路径是否为绝对路径
     *
     * @param  path 某个路径
     * @return true: 是绝对路径; false: 不是绝对路径
     */
    static bool is_absolute(const std::string &path);

    /**
     * @brief  整理用户输入的路径，去掉多余的/ . ..
     *
     * @param  path(in) 源路径
     * @return 如果是正确的路径则返回过滤后的，否则返回空
     */
    static std::string trim(const std::string &path);

    /**
     * @brief  检查给定目录是否存在，不存在则创建（非递归）
     *
     * @param  dir(in)   目录
     * @param  mode(in)  创建目录的权限
     * @return 成功返回true，失败返回false
     */
    static bool mkdir(const std::string &dir, mode_t mode);

    /**
     * @brief  检查给定目录是否存在，不存在则递归创建
     *
     * @param  dir(in)   目录
     * @param  mode(in)  创建目录的权限
     * @return 成功返回true，失败返回false
     */
    static bool mkdirs(const std::string &dir, mode_t mode);

    static bool is_dir(const std::string &path);

    static bool is_file(const std::string &path);

    static bool exist(const std::string &path);

    static bool nftw(const std::string &dir, NftwCallback cb, int depth, int flag);

    static const int MAX_PATH_LEN = 4096;

private:
    Path();
    ~Path();
};

} /* namespace bbts */

#endif // OP_OPED_NOAH_TOOLS_BBTS_AGENT_PATH_H
